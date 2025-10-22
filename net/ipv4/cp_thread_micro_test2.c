#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

dma_cap_mask_t mask_memcpy;

const struct file_operations micro_ops = {};

extern void *____memcpy(void *dest, const void *src, size_t n);
extern size_t __memcpy_avx_unaligned(void *dest, const void *src, size_t n);

// static inline unsigned int alignToPowerOfTwo(unsigned int value)
// {
// 	unsigned int mask;
// 	if (value == 0) {
// 		return 0;
// 	}
// 	mask = 1 << (sizeof(value) * 8 - 1);
// 	while ((value & mask) == 0) {
// 		mask >>= 1;
// 	}
// 	return (value & mask) == value ? value : mask;
// }
#define MICRO_TEST_TOTAL_PAGE (8192)
#define MICRO_TEST_TOTAL_LEN (MICRO_TEST_TOTAL_PAGE * PAGE_SIZE)

static inline void prep_buf(struct micro_cp_queue *queue, const unsigned int entry_size, int seq)
{
	int i = 0, j, index, block_len = entry_size;
	void *kva_src, *kva_dst;
	struct page *page_src, *page_dst;
	unsigned long entry_num;

	dma_cap_zero(mask_memcpy);
	dma_cap_set(DMA_MEMCPY, mask_memcpy);

	// block_len = alignToPowerOfTwo(entry_size);
	// if (block_len > PAGE_SIZE)
	// 	block_len = PAGE_SIZE;

	entry_num = MICRO_TEST_TOTAL_LEN / block_len;
	// descriptors = alloc_pages(GFP_KERNEL, get_order(sizeof(uint16_t) * entry_num));
	if (seq && block_len < 4096) {
		for (; i < entry_num / (PAGE_SIZE / block_len); i++) {
			page_src = alloc_pages(GFP_KERNEL, 0);
			kva_src = page_address(page_src);
			page_dst = alloc_pages(GFP_KERNEL, 0);
			kva_dst = page_address(page_dst);
			for (j = 0; j < PAGE_SIZE / block_len; j++) {
				index = (PAGE_SIZE / block_len) * i + j;
				queue->entries[index].page_from.page = page_src;
				queue->entries[index].page_from.kva = kva_src;
				queue->entries[index].page_to.page = page_dst;
				queue->entries[index].page_to.kva = kva_dst;
				queue->entries[index].from_offset = block_len * j;
				queue->entries[index].to_offset = block_len * j;
				queue->entries[index].length = block_len;
				queue->entries[index].descriptor = NULL;
				queue->entries[index].dma = false;
				queue->entries[index].type = MICRO_WORK;
			}
		}
	} else {
		for (; i < entry_num; i++) {
			page_src = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(block_len));
			kva_src = page_address(page_src);
			page_dst = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(block_len));
			kva_dst = page_address(page_dst);
			queue->entries[i].page_from.page = page_src;
			queue->entries[i].page_from.kva = kva_src;
			queue->entries[i].page_to.page = page_dst;
			queue->entries[i].page_to.kva = kva_dst;
			queue->entries[i].from_offset = 0;
			queue->entries[i].to_offset = 0;
			queue->entries[i].length = block_len;
			queue->entries[i].descriptor = NULL;
			queue->entries[i].dma = false;
			queue->entries[i].type = MICRO_WORK;
		}
	}

	queue->entries[entry_num].type = MICRO_END;
	queue->write_index = entry_num + 1;
	queue->thread_read_index = 0;

	queue->report_data.entry_length = block_len;
	queue->report_data.entry_num = entry_num;
	queue->report_data.start = 0;
	queue->report_data.end = 0;
}

static inline void release_buf(struct micro_cp_queue *queue)
{
	int i;
	struct micro_cp_entry *entry;
	struct page *last_from_page = NULL;
	for (i = 0; i < queue->report_data.entry_num; i++) {
		entry = &queue->entries[i];
		if (entry->page_from.page != last_from_page) {
			last_from_page = entry->page_from.page;
			__free_pages(entry->page_from.page, get_order(entry->length));
			__free_pages(entry->page_to.page, get_order(entry->length));
		}
	}
}

// chan = dma_request_chan_by_mask(&mask_memcpy); //TODO

static inline void issue_dma_memcpy(struct device *dma_dev, struct micro_cp_entry *entry)
{
	struct dma_async_tx_descriptor *chan_desc;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *chan = entry->chan;

	dma_addr_t src = dma_map_page(dma_dev, entry->page_from.page, entry->from_offset, entry->length, DMA_FROM_DEVICE);
	dma_addr_t dst = dma_map_page(dma_dev, entry->page_to.page, entry->to_offset, entry->length, DMA_TO_DEVICE);
	entry->dma = true;
	entry->page_from.dma_addr = src;
	entry->page_to.dma_addr = dst;

	chan_desc = dmaengine_prep_dma_memcpy(chan, dst, src, entry->length, flags);
	if (IS_ERR_OR_NULL(chan_desc)) { /* handle error */
		printk("chan_desc error\n");
	}
	entry->cookie = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan);
	return;
}

static inline enum dma_status check_dma_entry_completed(struct device *dma_dev, struct micro_cp_entry *entry)
{
	enum dma_status status = dma_async_is_tx_complete(entry->chan, entry->cookie, NULL, NULL);
	if (status == DMA_COMPLETE) {
		dma_unmap_page(dma_dev, entry->page_from.dma_addr, entry->length, DMA_FROM_DEVICE);
		dma_unmap_page(dma_dev, entry->page_to.dma_addr, entry->length, DMA_TO_DEVICE);
	}
	return status;
}

static inline int micro_thread_background_cp(void *ctx_void)
{
	unsigned int thread_read_index, usr_write_index, dma_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct micro_cp_queue *queue = ctx->micro_queue;
	struct micro_cp_entry *entry, *dma_entry;
	struct dma_device *dev;
	struct device *device;
	int chan_num;
	struct dma_chan **chans;
	int i = 1;
	bool dma = false;
	int dma_offset = 2;

	struct dma_chan *chan0 = dma_request_chan_by_mask(&mask_memcpy);
	if (IS_ERR(chan0))
		printk("no mem2mem dma dev\n");
	else {
		dma = true;
		dev = chan0->device;
		device = dev->dev;
		chan_num = dev->chancnt;
		chans = kmalloc_array(chan_num, sizeof(struct dma_chan *), GFP_KERNEL);
		printk("num of chans = %d\n", chan_num);
		chans[0] = chan0;
		for (; i < chan_num; i++)
			chans[i] = dma_request_chan_by_mask(&mask_memcpy);
	}

	KTHREAD_PREPARE_MM(ctx);
	if (queue->type == ERMS) {
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					____memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, entry->length);
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					break;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (dma && queue->type == DMA_SINGLE_CHAN) {
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					entry->dma = true;
					entry->chan = chan0;
					issue_dma_memcpy(device, entry);
					break;
				case MICRO_END:
					for (i = 0; i < thread_read_index; i++)
						while (check_dma_entry_completed(device, &queue->entries[i]) != DMA_COMPLETE)
							;
					queue->report_data.end = ktime_get_ns();
					break;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (dma && queue->type == DMA_FULL_CHAN) {
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					entry->dma = true;
					entry->chan = chans[thread_read_index % chan_num];
					issue_dma_memcpy(device, entry);
					break;
				case MICRO_END:
					for (i = 0; i < thread_read_index; i++)
						while (check_dma_entry_completed(device, &queue->entries[i]) != DMA_COMPLETE)
							;
					queue->report_data.end = ktime_get_ns();
					break;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (queue->type == AVX) {
		kernel_fpu_begin();
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					__memcpy_avx_unaligned(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset,
							       entry->length);
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					break;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
		kernel_fpu_end();
	} else if (dma && queue->type == COPIER) {
		kernel_fpu_begin();
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					if (!entry->dma) {
						dma_index = (thread_read_index + dma_offset) % MICRO_TEST_QUEUE_LEN;
						if (dma_index > thread_read_index && dma_index < usr_write_index) {
							dma_entry = &queue->entries[dma_index];
							if (dma_entry->type == MICRO_WORK && dma_entry->length >= 4096) {
								dma_entry->dma = true;
								dma_entry->chan = chans[0];
								issue_dma_memcpy(device, dma_entry);
							}
						}
						__memcpy_avx_unaligned(entry->page_to.kva + entry->to_offset,
								       entry->page_from.kva + entry->from_offset, entry->length);
					} else {
						if (check_dma_entry_completed(device, entry) != DMA_COMPLETE) {
							dma_offset++;
							while (check_dma_entry_completed(device, entry) != DMA_COMPLETE)
								;
						}
					}
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					break;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
		kernel_fpu_end();
	}
	KTHREAD_DROP_MM(ctx);

	if (dma)
		for (i = 0; i < chan_num; i++)
			dma_release_channel(chans[i]);

	return 0;
}

SYSCALL_DEFINE4(micro_create2, int, core, int, entry_size, int, type, int, seq)
{
	// printk("call syscall_create_cp_thread, core = %d, queue_type = %d\n", core, queue_type);
	struct copyer_ctx *ctx;
	int ret = -1, fd;
	struct file *file;

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);
	ctx->queue_type = QUEUE_TYPE_MICRO;
	ctx->should_wake_up = 0;
	ctx->should_stop = 0;
	ctx->micro_queue = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	prep_buf(ctx->micro_queue, entry_size, seq);

	ctx->micro_queue->type = type;

	file = anon_inode_getfile("[cp_thread_micro]", &micro_ops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		goto err;
	fd_install(fd, file);

	mmgrab(current->mm);
	ctx->mm = current->mm;
	if (core >= 0) {
		ret = -EINVAL;
		if (core >= nr_cpu_ids)
			goto err;
		if (!cpu_online(core))
			goto err;
		ctx->copyer_thread = kthread_create_on_cpu(micro_thread_background_cp, (void *)ctx, core, "copyer-micro");
	} else {
		ctx->copyer_thread = kthread_create(micro_thread_background_cp, (void *)ctx, "copyer-micro");
	}
	if (IS_ERR(ctx->copyer_thread)) {
		ret = PTR_ERR(ctx->copyer_thread);
		goto err;
	}
	wake_up_process(ctx->copyer_thread);

	return fd;
err:
	// kfree(ctx->queue);
	kfree(ctx);
	return ret;
}

SYSCALL_DEFINE2(micro_report2, int, fd, void *, report)
{
	// printk("call del_cp_thread\n");
	struct copyer_ctx *ctx;
	struct file *q_file = fget(fd);
	int ret;

	if (!q_file) {
		printk("[copyer] NO SUCH FILE");
		return -EFAULT;
	}
	ctx = (struct copyer_ctx *)(q_file->private_data);

	if (!ctx->micro_queue->report_data.end) {
		return -1;
	}

	ret = copy_to_user(report, &ctx->micro_queue->report_data, sizeof(struct mirco_report_data));
	if (ret != 0)
		printk("micro_report copy_to_user error!\n");

	ctx->should_stop = 1;
	kthread_stop(ctx->copyer_thread);
	ctx->copyer_thread = NULL;
	release_buf(ctx->micro_queue);
	__free_pages(virt_to_page(ctx->micro_queue), get_order(1 << 21));

	kfree(ctx);
	return 0;
}
