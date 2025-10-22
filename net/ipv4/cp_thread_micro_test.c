#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/highmem.h>
#include <copyer/copyer.h>

dma_cap_mask_t mask_memcpy;

extern void *____memcpy(void *dest, const void *src, size_t n);
extern void *__memcpy_avx_unaligned(void *dest, const void *src, size_t n);
extern void *____memcpy_avx(void *dest, const void *src, size_t n);

int micro_queue_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct copyer_ctx *ctx = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->micro_queue) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int empty_release(struct inode *inode, struct file *filp)
{
	return 0;
}

const struct file_operations micro_ops = {
	.mmap = micro_queue_mmap,
	.release = empty_release,
};
// static inline void prep_buf(struct micro_cp_queue *queue, const unsigned int entry_size, int seq)
// {
// 	int i = 0, j, index, block_len = entry_size;
// 	void *kva_src, *kva_dst;
// 	struct page *page_src, *page_dst;
// 	unsigned long entry_num;

// 	block_len = alignToPowerOfTwo(entry_size);
// 	if (block_len > PAGE_SIZE)
// 		block_len = PAGE_SIZE;

// 	entry_num = MICRO_TEST_TOTAL_LEN / block_len;
// 	descriptors = alloc_pages(GFP_KERNEL, get_order(sizeof(uint16_t) * entry_num));
// 	if (seq && block_len < 4096) {
// 		for (; i < entry_num / (PAGE_SIZE / block_len); i++) {
// 			page_src = alloc_pages(GFP_KERNEL, 0);
// 			kva_src = page_address(page_src);
// 			page_dst = alloc_pages(GFP_KERNEL, 0);
// 			kva_dst = page_address(page_dst);
// 			for (j = 0; j < PAGE_SIZE / block_len; j++) {
// 				index = (PAGE_SIZE / block_len) * i + j;
// 				queue->entries[index].page_from.page = page_src;
// 				queue->entries[index].page_from.kva = kva_src;
// 				queue->entries[index].page_to.page = page_dst;
// 				queue->entries[index].page_to.kva = kva_dst;
// 				queue->entries[index].from_offset = block_len * j;
// 				queue->entries[index].to_offset = block_len * j;
// 				queue->entries[index].length = block_len;
// 				queue->entries[index].descriptor = NULL;
// 				queue->entries[index].dma = false;
// 				queue->entries[index].type = MICRO_WORK;
// 			}
// 		}
// 	} else {
// 		for (; i < entry_num; i++) {
// 			page_src = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(block_len));
// 			kva_src = page_address(page_src);
// 			page_dst = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(block_len));
// 			kva_dst = page_address(page_dst);
// 			queue->entries[i].page_from.page = page_src;
// 			queue->entries[i].page_from.kva = kva_src;
// 			queue->entries[i].page_to.page = page_dst;
// 			queue->entries[i].page_to.kva = kva_dst;
// 			queue->entries[i].from_offset = 0;
// 			queue->entries[i].to_offset = 0;
// 			queue->entries[i].length = block_len;
// 			queue->entries[i].descriptor = NULL;
// 			queue->entries[i].dma = false;
// 			queue->entries[i].type = MICRO_WORK;
// 		}
// 	}

// 	queue->entries[entry_num].type = MICRO_END;
// 	queue->write_index = entry_num + 1;
// 	queue->thread_read_index = 0;

// 	queue->report_data.entry_length = block_len;
// 	queue->report_data.entry_num = entry_num;
// 	queue->report_data.start = 0;
// 	queue->report_data.end = 0;
// }

// static inline void release_buf(struct micro_cp_queue *queue)
// {
// 	int i;
// 	struct micro_cp_entry *entry;
// 	struct page *last_from_page = NULL;
// 	for (i = 0; i < queue->report_data.entry_num; i++) {
// 		entry = &queue->entries[i];
// 		if (entry->page_from.page != last_from_page) {
// 			last_from_page = entry->page_from.page;
// 			__free_pages(entry->page_from.page, get_order(entry->length));
// 			__free_pages(entry->page_to.page, get_order(entry->length));
// 		}
// 	}
// }

// chan = dma_request_chan_by_mask(&mask_memcpy); //TODO

static inline void issue_dma_memcpy_nocache(struct device *dma_dev, struct micro_cp_entry *entry)
{
	struct dma_async_tx_descriptor *chan_desc;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *chan = entry->chan;
	struct page **page_from, **page_to;
	dma_addr_t src, dst;
	int ret;
	int page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;

	page_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
	page_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);

	ret = get_user_pages_fast((unsigned long)entry->from, page_num, 0, page_from);
	if (ret != page_num) {
		printk("get_user_pages_fast error, %d\n", ret);
		return;
	}
	ret = get_user_pages_fast((unsigned long)entry->to, page_num, 1, page_to);
	if (ret != page_num) {
		printk("get_user_pages_fast  , %d\n", ret);
		return;
	}

	src = dma_map_page(dma_dev, page_from[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
	dst = dma_map_page(dma_dev, page_to[0], entry->to_offset, entry->length, DMA_TO_DEVICE);
	entry->dma = true;
	entry->dma_addr_from = src;
	entry->dma_addr_to = dst;

	chan_desc = dmaengine_prep_dma_memcpy(chan, dst, src, entry->length, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan);

	kfree(page_from);
	kfree(page_to);
	return;
}

static inline void issue_dma_memcpy_cache(struct micro_cp_queue *queue, struct device *dma_dev, struct micro_cp_entry *entry)
{
	struct dma_async_tx_descriptor *chan_desc;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *chan = entry->chan;
	struct page **page_from, **page_to;
	dma_addr_t src, dst;
	struct va2dma_cache_node *from_dma_cache_node, *to_dma_cache_node;
	bool from_cached = false, to_cached = false;
	int page_num;

	hash_for_each_possible (queue->va2dma_cache, from_dma_cache_node, node, (u64)entry->from) {
		if (from_dma_cache_node->va == entry->from) {
			from_cached = true;
			break;
		}
	}

	hash_for_each_possible (queue->va2dma_cache, to_dma_cache_node, node, (u64)entry->to) {
		if (to_dma_cache_node->va == entry->to) {
			to_cached = true;
			break;
		}
	}

	if (!from_cached) {
		page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;
		page_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)entry->from, page_num, 0, page_from);
		src = dma_map_page(dma_dev, page_from[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
		from_dma_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		from_dma_cache_node->pages = page_from;
		from_dma_cache_node->va = entry->from;
		from_dma_cache_node->dma = src;
		from_dma_cache_node->length = entry->length;
		from_dma_cache_node->dir = DMA_FROM_DEVICE;
		hash_add(queue->va2dma_cache, &from_dma_cache_node->node, (u64)entry->from);
	} else {
		if (from_dma_cache_node->dma)
			src = from_dma_cache_node->dma;
		else {
			src = dma_map_page(dma_dev, from_dma_cache_node->pages[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
			from_dma_cache_node->dma = src;
		}
	}

	if (!to_cached) {
		page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;
		page_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)entry->to, page_num, 1, page_to);
		dst = dma_map_page(dma_dev, page_to[0], entry->to_offset, entry->length, DMA_TO_DEVICE);
		to_dma_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		to_dma_cache_node->pages = page_to;
		to_dma_cache_node->va = entry->to;
		to_dma_cache_node->dma = dst;
		to_dma_cache_node->length = entry->length;
		to_dma_cache_node->dir = DMA_TO_DEVICE;
		hash_add(queue->va2dma_cache, &to_dma_cache_node->node, (u64)entry->to);
	} else {
		if (to_dma_cache_node->dma)
			dst = to_dma_cache_node->dma;
		else {
			dst = dma_map_page(dma_dev, to_dma_cache_node->pages[0], entry->to_offset, entry->length, DMA_FROM_DEVICE);
			to_dma_cache_node->dma = dst;
		}
	}

	entry->dma = true;

	chan_desc = dmaengine_prep_dma_memcpy(chan, dst, src, entry->length, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan);
	return;
}

static inline void issue_dma_memcpy_nocache_fullchan(struct device *dma_dev, struct micro_cp_entry *entry)
{
	struct dma_async_tx_descriptor *chan_desc;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *chan = entry->chan;
	struct dma_chan *chan2 = entry->chan2;
	struct page **page_from, **page_to;
	dma_addr_t src, dst;
	int ret;
	int page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;
	long half_len = entry->length / 2;

	page_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
	page_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);

	ret = get_user_pages_fast((unsigned long)entry->from, page_num, 0, page_from);
	if (ret != page_num) {
		printk("get_user_pages_fast error, %d\n", ret);
		return;
	}
	ret = get_user_pages_fast((unsigned long)entry->to, page_num, 1, page_to);
	if (ret != page_num) {
		printk("get_user_pages_fast  , %d\n", ret);
		return;
	}

	src = dma_map_page(dma_dev, page_from[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
	dst = dma_map_page(dma_dev, page_to[0], entry->to_offset, entry->length, DMA_TO_DEVICE);
	entry->dma = true;
	entry->dma_dual_chan = true;
	entry->dma_addr_from = src;
	entry->dma_addr_to = dst;

	chan_desc = dmaengine_prep_dma_memcpy(chan, dst, src, half_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan);

	chan_desc = dmaengine_prep_dma_memcpy(chan2, dst + half_len, src + half_len, half_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie2 = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan2);

	kfree(page_from);
	kfree(page_to);
	return;
}

static inline void issue_dma_memcpy_cache_fullchan(struct micro_cp_queue *queue, struct device *dma_dev, struct micro_cp_entry *entry)
{
	struct dma_async_tx_descriptor *chan_desc;
	const enum dma_ctrl_flags flags = DMA_CTRL_ACK;
	struct dma_chan *chan = entry->chan;
	struct dma_chan *chan2 = entry->chan2;
	struct page **page_from, **page_to;
	dma_addr_t src, dst;
	struct va2dma_cache_node *from_dma_cache_node, *to_dma_cache_node;
	bool from_cached = false, to_cached = false;
	int page_num;
	long half_len = entry->length / 2;

	hash_for_each_possible (queue->va2dma_cache, from_dma_cache_node, node, (u64)entry->from) {
		if (from_dma_cache_node->va == entry->from) {
			from_cached = true;
			break;
		}
	}

	hash_for_each_possible (queue->va2dma_cache, to_dma_cache_node, node, (u64)entry->to) {
		if (to_dma_cache_node->va == entry->to) {
			to_cached = true;
			break;
		}
	}

	if (!from_cached) {
		page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;
		page_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)entry->from, page_num, 0, page_from);
		src = dma_map_page(dma_dev, page_from[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
		from_dma_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		from_dma_cache_node->pages = page_from;
		from_dma_cache_node->va = entry->from;
		from_dma_cache_node->dma = src;
		from_dma_cache_node->length = entry->length;
		from_dma_cache_node->dir = DMA_FROM_DEVICE;
		hash_add(queue->va2dma_cache, &from_dma_cache_node->node, (u64)entry->from);
	} else {
		if (from_dma_cache_node->dma)
			src = from_dma_cache_node->dma;
		else {
			src = dma_map_page(dma_dev, from_dma_cache_node->pages[0], entry->from_offset, entry->length, DMA_FROM_DEVICE);
			from_dma_cache_node->dma = src;
		}
	}

	if (!to_cached) {
		page_num = (entry->length > PAGE_SIZE) ? entry->length / PAGE_SIZE : 1;
		page_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)entry->to, page_num, 1, page_to);
		dst = dma_map_page(dma_dev, page_to[0], entry->to_offset, entry->length, DMA_TO_DEVICE);
		to_dma_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		to_dma_cache_node->pages = page_to;
		to_dma_cache_node->va = entry->to;
		to_dma_cache_node->dma = dst;
		to_dma_cache_node->length = entry->length;
		to_dma_cache_node->dir = DMA_TO_DEVICE;
		hash_add(queue->va2dma_cache, &to_dma_cache_node->node, (u64)entry->to);
	} else {
		if (to_dma_cache_node->dma)
			dst = to_dma_cache_node->dma;
		else {
			dst = dma_map_page(dma_dev, to_dma_cache_node->pages[0], entry->to_offset, entry->length, DMA_FROM_DEVICE);
			to_dma_cache_node->dma = dst;
		}
	}

	entry->dma = true;
	entry->dma_dual_chan = true;

	chan_desc = dmaengine_prep_dma_memcpy(chan, dst, src, half_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan);

	chan_desc = dmaengine_prep_dma_memcpy(chan2, dst + half_len, src + half_len, half_len, flags);
	if (IS_ERR_OR_NULL(chan_desc)) {
		printk("chan_desc error\n");
	}
	entry->cookie2 = dmaengine_submit(chan_desc);
	dma_async_issue_pending(chan2);
	return;
}

static inline void memcpy_nocache(unsigned int length, void __user *from_usr, void __user *to_usr, unsigned int from_offset, unsigned int to_offset,
				  void *(*memcpy_func)(void *, const void *, size_t))
{
	int page_num;
	struct page **pages_from, **pages_to;
	void *from, *to;
	page_num = (length > PAGE_SIZE) ? length / PAGE_SIZE : 1;
	pages_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
	pages_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
	get_user_pages_fast((unsigned long)from_usr, page_num, 0, pages_from);
	get_user_pages_fast((unsigned long)to_usr, page_num, 1, pages_to);
	from = kmap(pages_from[0]);
	to = kmap(pages_to[0]);
	memcpy_func(to + to_offset, from + from_offset, length);
	kunmap(pages_from[0]);
	kunmap(pages_to[0]);
	kfree(pages_from);
	kfree(pages_to);
}

static inline void memcpy_cache(struct micro_cp_queue *queue, unsigned int length, void __user *from_usr, void __user *to_usr,
				unsigned int from_offset, unsigned int to_offset, void *(*memcpy_func)(void *, const void *, size_t))
{
	int page_num;
	struct page **pages_from, **pages_to;
	void *from, *to;
	bool from_cached = false, to_cached = false;
	struct va2dma_cache_node *from_cache_node, *to_cache_node;

	hash_for_each_possible (queue->va2dma_cache, from_cache_node, node, (u64)from_usr) {
		if (from_cache_node->va == from_usr) {
			from_cached = true;
			break;
		}
	}

	hash_for_each_possible (queue->va2dma_cache, to_cache_node, node, (u64)to_usr) {
		if (to_cache_node->va == to_usr) {
			to_cached = true;
			break;
		}
	}

	if (!from_cached) {
		page_num = (length > PAGE_SIZE) ? length / PAGE_SIZE : 1;
		pages_from = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)from_usr, page_num, 0, pages_from);
		from = kmap(pages_from[0]);
		from_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		from_cache_node->va = from_usr;
		from_cache_node->kva = from;
		from_cache_node->pages = pages_from;
		hash_add(queue->va2dma_cache, &from_cache_node->node, (u64)from_usr);
	} else {
		if (from_cache_node->kva)
			from = from_cache_node->kva;
		else {
			from = kmap(from_cache_node->pages[0]);
			from_cache_node->kva = from;
		}
	}

	if (!to_cached) {
		pages_to = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)to_usr, page_num, 1, pages_to);
		to = kmap(pages_to[0]);
		to_cache_node = &queue->cache_poll[queue->cache_pool_index];
		queue->cache_pool_index++;
		to_cache_node->va = to_usr;
		to_cache_node->kva = to;
		to_cache_node->pages = pages_to;
		hash_add(queue->va2dma_cache, &to_cache_node->node, (u64)to_usr);
	} else {
		if (to_cache_node->kva)
			to = to_cache_node->kva;
		else {
			to = kmap(to_cache_node->pages[0]);
			to_cache_node->kva = to;
		}
	}

	memcpy_func(to + to_offset, from + from_offset, length);
}

static inline enum dma_status check_dma_entry_completed_nocache(struct device *dma_dev, struct micro_cp_entry *entry)
{
	enum dma_status status, status2;
	if(entry->dma_dual_chan){
		status = dma_async_is_tx_complete(entry->chan, entry->cookie, NULL, NULL);
		status2 = dma_async_is_tx_complete(entry->chan2, entry->cookie2, NULL, NULL);
		if (status == DMA_COMPLETE && status2 == DMA_COMPLETE) {
			dma_unmap_page(dma_dev, entry->dma_addr_from, entry->length, DMA_FROM_DEVICE);
			dma_unmap_page(dma_dev, entry->dma_addr_to, entry->length, DMA_TO_DEVICE);
			return status;
		}
		return DMA_IN_PROGRESS;
	}
	status = dma_async_is_tx_complete(entry->chan, entry->cookie, NULL, NULL);
	if (status == DMA_COMPLETE) {
		dma_unmap_page(dma_dev, entry->dma_addr_from, entry->length, DMA_FROM_DEVICE);
		dma_unmap_page(dma_dev, entry->dma_addr_to, entry->length, DMA_TO_DEVICE);
	}
	return status;
}

static inline enum dma_status check_dma_entry_completed_cache(struct device *dma_dev, struct micro_cp_entry *entry)
{
	enum dma_status status, status2;
	if(entry->dma_dual_chan){
		status = dma_async_is_tx_complete(entry->chan, entry->cookie, NULL, NULL);
		status2 = dma_async_is_tx_complete(entry->chan2, entry->cookie2, NULL, NULL);
		if (status == DMA_COMPLETE && status2 == DMA_COMPLETE)
			return status;
		return DMA_IN_PROGRESS;
	}
	status = dma_async_is_tx_complete(entry->chan, entry->cookie, NULL, NULL);
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
	int dma_queue_len = 0, memcpy_continuous_len = 0;
	const int dma_max_queue_len = 3;
	const int memcpy_max_continuous_len = 5;

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
	while (!queue->run)
		;
	if (ctx->queue_type == ERMS) {
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					memcpy_nocache(entry->length, entry->from, entry->to, entry->from_offset, entry->to_offset, ____memcpy);
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (dma && ctx->queue_type == DMA_SINGLE_CHAN) {
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
					issue_dma_memcpy_nocache(device, entry);
					while (check_dma_entry_completed_nocache(device, entry) != DMA_COMPLETE)
						;
					break;
				case MICRO_END:
					// for (i = 0; i < thread_read_index; i++)
					// 	while (check_dma_entry_completed_nocache(device, &queue->entries[i]) != DMA_COMPLETE)
					// 		;
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (dma && ctx->queue_type == DMA_FULL_CHAN) {
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					entry->dma = true;
					entry->chan = chans[0];
					entry->chan2 = chans[1];
					issue_dma_memcpy_nocache_fullchan(device, entry);
					while (check_dma_entry_completed_nocache(device, entry) != DMA_COMPLETE)
						;
					break;
				case MICRO_END:
					// for (i = 0; i < thread_read_index; i++)
					// 	while (check_dma_entry_completed_nocache(device, &queue->entries[i]) != DMA_COMPLETE)
					// 		;
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
	} else if (ctx->queue_type == AVX) {
		kernel_fpu_begin();
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					if (entry->length > PAGE_SIZE)
						memcpy_nocache(entry->length, entry->from, entry->to, entry->from_offset, entry->to_offset,
							       __memcpy_avx_unaligned);
					else
						memcpy_nocache(entry->length, entry->from, entry->to, entry->from_offset, entry->to_offset,
							       ____memcpy_avx);
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
		kernel_fpu_end();
	} else if (dma && ctx->queue_type == COPIER) {
		kernel_fpu_begin();
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					if(entry->length < 1600){
						memcpy_cache(queue, entry->length, entry->from, entry->to, entry->from_offset,
								     entry->to_offset, ____memcpy_avx);
						break;
					}

					if (memcpy_continuous_len < memcpy_max_continuous_len) {
						dma_index = (thread_read_index + memcpy_max_continuous_len) % MICRO_TEST_QUEUE_LEN;
						if (dma_queue_len < dma_max_queue_len && dma_index > thread_read_index &&
						    dma_index < usr_write_index) {
							dma_entry = &queue->entries[dma_index];
							if (dma_entry->type == MICRO_WORK) {
								dma_entry->dma = true;
								dma_entry->chan = chans[0];
								if(entry->length > 8192){
									dma_entry->chan2 = chans[1];
									issue_dma_memcpy_cache_fullchan(queue, device, dma_entry);
								}
								else
									issue_dma_memcpy_cache(queue, device, dma_entry);
								dma_queue_len++;
							}
						}
						if (entry->length > PAGE_SIZE)
							memcpy_cache(queue, entry->length, entry->from, entry->to, entry->from_offset,
								     entry->to_offset, __memcpy_avx_unaligned);
						else
							memcpy_cache(queue, entry->length, entry->from, entry->to, entry->from_offset,
								     entry->to_offset, ____memcpy_avx);
						memcpy_continuous_len++;
					} else {
						dma_queue_len--;
						if (!dma_queue_len)
							memcpy_continuous_len = 0;
						if (check_dma_entry_completed_cache(device, entry) != DMA_COMPLETE) {
							// dma_offset++;
							while (check_dma_entry_completed_cache(device, entry) != DMA_COMPLETE)
								;
						}
					}
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
		kernel_fpu_end();
	} else if (dma && ctx->queue_type == COPIER_NOCACHE) {
		kernel_fpu_begin();
		queue->report_data.start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue->thread_read_index;
			usr_write_index = queue->write_index;

			if (thread_read_index != usr_write_index) {
				entry = &queue->entries[thread_read_index];
				switch (entry->type) {
				case MICRO_WORK:
					if (memcpy_continuous_len < memcpy_max_continuous_len) {
						dma_index = (thread_read_index + memcpy_max_continuous_len) % MICRO_TEST_QUEUE_LEN;
						if (dma_queue_len < dma_max_queue_len && dma_index > thread_read_index &&
						    dma_index < usr_write_index) {
							dma_entry = &queue->entries[dma_index];
							if (dma_entry->type == MICRO_WORK) {
								dma_entry->dma = true;
								dma_entry->chan = chans[0];
								if(entry->length > 8192){
									dma_entry->chan2 = chans[1];
									issue_dma_memcpy_nocache_fullchan(device, dma_entry);
								}
								else
									issue_dma_memcpy_nocache(device, dma_entry);
								dma_queue_len++;
							}
						}
						if (entry->length > PAGE_SIZE)
							memcpy_nocache(entry->length, entry->from, entry->to, entry->from_offset, entry->to_offset,
								       __memcpy_avx_unaligned);
						else
							memcpy_nocache(entry->length, entry->from, entry->to, entry->from_offset, entry->to_offset,
								       ____memcpy_avx);
						memcpy_continuous_len++;
					} else {
						dma_queue_len--;
						if (!dma_queue_len)
							memcpy_continuous_len = 0;
						if (check_dma_entry_completed_nocache(device, entry) != DMA_COMPLETE) {
							// dma_offset++;
							while (check_dma_entry_completed_nocache(device, entry) != DMA_COMPLETE)
								;
						}
					}
					break;
				case MICRO_END:
					queue->report_data.end = ktime_get_ns();
					queue->end = true;
				}
				queue->thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
			}
		}
		kernel_fpu_end();
	}

	if (dma) {
		for (i = 0; i < chan_num; i++)
			dma_release_channel(chans[i]);
	}
	for (i = 0; i < queue->cache_pool_index; i++) {
		if (queue->cache_poll[i].kva)
			kunmap(queue->cache_poll[i].pages[0]);
		if (queue->cache_poll[i].dma)
			dma_unmap_page(device, queue->cache_poll[i].dma, queue->cache_poll[i].length, queue->cache_poll[i].dir);
		kfree(queue->cache_poll[i].pages);
	}

	KTHREAD_DROP_MM(ctx);
	return 0;
}

SYSCALL_DEFINE4(micro_create, int, core, int, entry_size, int, type, int, seq)
{
	struct copyer_ctx *ctx;
	int ret = -1, fd;
	struct file *file;
	int i;

	dma_cap_zero(mask_memcpy);
	dma_cap_set(DMA_MEMCPY, mask_memcpy);

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);
	ctx->queue_type = type;
	ctx->should_wake_up = 0;
	ctx->should_stop = 0;
	ctx->micro_queue = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	// prep_buf(ctx->micro_queue, entry_size, seq);
	memset(ctx->micro_queue, 0, sizeof(struct micro_cp_queue));
	for (i = 0; i < (1 << HASH_TABLE_BITS); i++)
		INIT_HLIST_HEAD(&ctx->micro_queue->va2dma_cache[i]);

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

SYSCALL_DEFINE2(micro_report, int, fd, void *, report)
{
	struct copyer_ctx *ctx;
	struct file *q_file = fget(fd);
	int ret;

	if (!q_file) {
		printk("[copyer] NO SUCH FILE");
		return -EFAULT;
	}
	ctx = (struct copyer_ctx *)(q_file->private_data);

	if (!ctx->micro_queue->end) {
		return -1;
	}

	ret = copy_to_user(report, &ctx->micro_queue->report_data, sizeof(struct mirco_report_data));
	if (ret != 0)
		printk("micro_report copy_to_user error!\n");

	ctx->should_stop = 1;
	kthread_stop(ctx->copyer_thread);
	ctx->copyer_thread = NULL;
	// release_buf(ctx->micro_queue);
	__free_pages(virt_to_page(ctx->micro_queue), get_order(1 << 21));

	kfree(ctx);
	return 0;
}
