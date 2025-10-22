#include <copyer/cow.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>

struct task_struct *cow_copy_thread;
struct cow_cp_queue cow_copy_queue;

/* tool func */
inline void asyc_copy_huge_page(struct page *src, struct page *dst, volatile uint8_t *status)
{
	int write_index = cow_copy_queue.write_index;
	struct cow_entry *entry = &cow_copy_queue.entries[write_index];
	entry->src = src;
	entry->dst = dst;
	entry->status = status;
	entry->is_huge_page = true;
	cow_copy_queue.write_index = (write_index + 1) % COW_ENTRY_NUM;
}

inline void asyc_copy_page(struct page *src, struct page *dst, volatile uint8_t *status)
{
	int write_index = cow_copy_queue.write_index;
	struct cow_entry *entry = &cow_copy_queue.entries[write_index];
	entry->src = src;
	entry->dst = dst;
	entry->status = status;
	cow_copy_queue.write_index = (write_index + 1) % COW_ENTRY_NUM;
}
/* tool func end*/

extern void *__memcpy_avx_unaligned(void *dest, const void *src, size_t n);
extern void *____memcpy_avx(void *dest, const void *src, size_t n);

// TODO: compare the performance of __memcpy_avx and __memcpy_avx_unaligned
void memcpy_page_avx_4k(struct cow_entry *entry)
{
	void *v_src, *v_dst;

	v_src = kmap(entry->src);
	v_dst = kmap(entry->dst);
	__memcpy_avx_unaligned(v_dst, v_src, PAGE_SIZE);
	kunmap(entry->src);
	kunmap(entry->dst);

	*(entry->status) = 1;
}

// TODO: adjust the DMA_PER_CHAN_RATE and AVX_RATE
void memcpy_huge_page_avx_dma_2m(struct cow_cp_queue *queue, struct cow_entry *entry)
{
	// unsigned i, nr = compound_nr(entry->src);
	struct dma_async_tx_descriptor *chan_desc;
	dma_cookie_t cookie1, cookie2;
	enum dma_status status;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK;
	const long dma_per_chan_len = DMA_PER_CHAN_PAGE * PAGE_SIZE;
	void *v_src, *v_dst;

	dma_addr_t d_src = dma_map_page(queue->dma_dev, entry->src + AVX_PAGE, 0, 2 * dma_per_chan_len, DMA_FROM_DEVICE);
	dma_addr_t d_dst = dma_map_page(queue->dma_dev, entry->dst + AVX_PAGE, 0, 2 * dma_per_chan_len, DMA_TO_DEVICE);

	chan_desc = dmaengine_prep_dma_memcpy(queue->chan1, d_dst, d_src, dma_per_chan_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	cookie1 = dmaengine_submit(chan_desc);
	dma_async_issue_pending(queue->chan1);

	chan_desc = dmaengine_prep_dma_memcpy(queue->chan2, d_dst + dma_per_chan_len, d_src + dma_per_chan_len, dma_per_chan_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	cookie2 = dmaengine_submit(chan_desc);
	dma_async_issue_pending(queue->chan2);

	v_src = kmap(entry->src);
	v_dst = kmap(entry->dst);
	__memcpy_avx_unaligned(v_dst, v_src, AVX_PAGE * PAGE_SIZE);
	kunmap(entry->src);
	kunmap(entry->dst);

	status = dma_async_is_tx_complete(queue->chan1, cookie1, NULL, NULL);
	while (status != DMA_COMPLETE)
		status = dma_async_is_tx_complete(queue->chan1, cookie1, NULL, NULL);
	dma_unmap_page(queue->dma_dev, d_src, 2 * dma_per_chan_len, DMA_FROM_DEVICE);

	status = dma_async_is_tx_complete(queue->chan2, cookie2, NULL, NULL);
	while (status != DMA_COMPLETE)
		status = dma_async_is_tx_complete(queue->chan2, cookie2, NULL, NULL);
	dma_unmap_page(queue->dma_dev, d_dst, 2 * dma_per_chan_len, DMA_TO_DEVICE);

	*(entry->status) = 1;
}

static inline void prepare_queue(void)
{
	dma_cap_mask_t mask_memcpy;

	dma_cap_zero(mask_memcpy);
	dma_cap_set(DMA_MEMCPY, mask_memcpy);

	memset(&cow_copy_queue, 0, sizeof(struct cow_cp_queue));

	cow_copy_queue.chan1 = dma_request_chan_by_mask(&mask_memcpy);
	cow_copy_queue.chan2 = dma_request_chan_by_mask(&mask_memcpy);
	cow_copy_queue.dma_dev = cow_copy_queue.chan1->device->dev;
}

static inline void release_queue(void)
{
	dma_release_channel(cow_copy_queue.chan1);
	dma_release_channel(cow_copy_queue.chan2);
}

static inline int cow_thread_background(void *)
{
	unsigned int thread_read_index, usr_write_index;

	struct cow_entry *entry;

	while (!cow_copy_queue.should_stop) {
		thread_read_index = cow_copy_queue.thread_read_index;
		usr_write_index = cow_copy_queue.write_index;

		if (thread_read_index != usr_write_index) {
			entry = &cow_copy_queue.entries[thread_read_index];
			if (entry->is_huge_page)
				memcpy_huge_page_avx_dma_2m(&cow_copy_queue, entry);
			else
				memcpy_page_avx_4k(entry);
			cow_copy_queue.thread_read_index = (thread_read_index + 1) % COW_ENTRY_NUM;
		}
	}
	return 0;
}

SYSCALL_DEFINE1(cow_copy_thread_create, int, core)
{
	int ret = -1;

	prepare_queue();
	if (!cow_copy_thread) {
		if (core >= 0) {
			ret = -EINVAL;
			if (core >= nr_cpu_ids)
				goto err;
			if (!cpu_online(core))
				goto err;
			cow_copy_thread = kthread_create_on_cpu(cow_thread_background, NULL, core, "copyer-cow");
		} else
			cow_copy_thread = kthread_create(cow_thread_background, NULL, "copyer-cow");
	}
	if (IS_ERR(cow_copy_thread)) {
		ret = PTR_ERR(cow_copy_thread);
		goto err;
	}
	wake_up_process(cow_copy_thread);
	return 0;
err:
	release_queue();
	return ret;
}

SYSCALL_DEFINE0(cow_copy_thread_delete)
{
	if (cow_copy_thread) {
		cow_copy_queue.should_stop = true;
		kthread_stop(cow_copy_thread);
		cow_copy_thread = NULL;
	}
	return 0;
}
