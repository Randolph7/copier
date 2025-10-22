#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

static dma_cap_mask_t mask_memcpy;

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
#define MICRO_TEST_TOTAL_PAGE (4096)
#define MICRO_TEST_TOTAL_LEN (MICRO_TEST_TOTAL_PAGE * PAGE_SIZE)

static inline void prep_buf(struct micro_cp_queue *queue, const unsigned int entry_size)
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
	if (entry_num > MICRO_TEST_QUEUE_LEN - 2)
		entry_num = MICRO_TEST_QUEUE_LEN - 2;

	// descriptors = alloc_pages(GFP_KERNEL, get_order(sizeof(uint16_t) * entry_num));
	if (block_len <= 4096) {
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

static inline int micro_thread_background_cp(struct copyer_ctx *ctx)
{
	unsigned int thread_read_index, usr_write_index;
	struct micro_cp_queue *queue = ctx->micro_queue;
	struct micro_cp_entry *entry;
	struct dma_device *dev;
	struct device *device;
	int chan_num;
	struct dma_chan **chans;
	int i = 1;
	bool dma = false;

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
			} else {
				break;
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
			} else {
				break;
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
			} else {
				break;
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
			} else {
				break;
			}
		}
		kernel_fpu_end();
	}

	if (dma)
		for (i = 0; i < chan_num; i++)
			dma_release_channel(chans[i]);

	return 0;
}

SYSCALL_DEFINE3(micro_create2, int, entry_size, int, type, void *, report)
{
	// printk("call syscall_create_cp_thread, core = %d, queue_type = %d\n", core, queue_type);
	struct copyer_ctx *ctx;
	int ret = -1;

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);
	ctx->queue_type = QUEUE_TYPE_MICRO;
	ctx->should_wake_up = 0;
	ctx->should_stop = 0;
	ctx->micro_queue = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	prep_buf(ctx->micro_queue, entry_size);

	ctx->micro_queue->type = type;

	micro_thread_background_cp(ctx);
	ret = copy_to_user(report, &ctx->micro_queue->report_data, sizeof(struct mirco_report_data));
	release_buf(ctx->micro_queue);
	__free_pages(virt_to_page(ctx->micro_queue), get_order(1 << 21));
	kfree(ctx);
	return ret;
}
