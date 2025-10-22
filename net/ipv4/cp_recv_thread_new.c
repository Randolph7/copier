#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>
// #include "cp_memlog.h"

#define COPYLENGTH 1024

extern size_t __memcpy_avx_unaligned(void *dest, const void *src, size_t n);

inline void check_sync_task(struct sync_queue *sync_queue, struct cp_queue *cp_queue)
{
	unsigned int thread_read_index;
	struct sync_entry *sync_entry;
	struct cp_entry *cp_entry;
	u8 *vaddr;
	unsigned int cp_queue_tail, cp_queue_head;
	int sync_len, cp_len;
	void *cp_entry_end, *sync_entry_end, *sync_entry_start, *cp_entry_start;

	thread_read_index = sync_queue->thread_read_index;
	if (thread_read_index == sync_queue->write_index)
		return;
	sync_entry = &sync_queue->entries[thread_read_index];
	// printk("process copy task to=%lu len=%d\n", (u64)sync_entry->to_va, sync_entry->length);
	sync_len = sync_entry->length;
	sync_entry_end = sync_entry->to_va + sync_len;
	sync_entry_start = sync_entry->to_va;

	cp_queue_tail = cp_queue->thread_read_index;
	cp_queue_head = cp_queue->write_index;

	while (cp_queue_tail != cp_queue_head) {
		cp_entry = &cp_queue->entries[cp_queue_tail];

		if (cp_entry->type == TYPE_RECV_SOCKET_DATA) {
			cp_entry_start = cp_entry->to_va;
			cp_entry_end = cp_entry_start + cp_entry->length;

			if (cp_entry_start <= sync_entry_start && cp_entry_end >= sync_entry_end) {
				// printk("sync %d\n", sync_len);
				vaddr = kmap(cp_entry->page);
				copyout_copier(sync_entry_start, vaddr + cp_entry->from_offset + (sync_entry_start - cp_entry_start), sync_len);
				kunmap(cp_entry->page);
				sync_entry->status = 1;
				sync_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_SYNC_ENTRY_NUM;
				return;
			} else if (cp_entry_start <= sync_entry_start && sync_entry_start < cp_entry_end) {
				// printk("sync %d\n", cp_len);
				cp_len = cp_entry_end - sync_entry_start;
				vaddr = kmap(cp_entry->page);
				copyout_copier(sync_entry_start, vaddr + cp_entry->from_offset + (sync_entry_start - cp_entry_start), cp_len);
				kunmap(cp_entry->page);
				sync_len -= cp_len;
				sync_entry_start += cp_len;
			}
		}
		cp_queue_tail = (cp_queue_tail + 1) % DEFUALT_CP_ENTRY_NUM;
	}
	sync_entry->status = 1;
	sync_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_SYNC_ENTRY_NUM;
}

static inline int skb_copy_out(struct cp_entry *entry, struct sync_queue *sync_queue, struct cp_queue *cp_queue)
{
	// printk("skb_copy_out to=%lx len=%lu\n", entry->to_va, entry->length);
	void *dst = entry->to_va;
	const long offset = (unsigned long)entry->to_va - (unsigned long)entry->to_va_base;
	u8 *vaddr = kmap(entry->page);
	// struct page *pages[16];
	// int ret;
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = entry->length;

	// long num_pages = ((unsigned long)dst + remainingLength - ((unsigned long)dst & PAGE_MASK) + PAGE_SIZE - 1) / PAGE_SIZE;
	// ret = get_user_pages_fast((unsigned long)dst, num_pages, FOLL_WRITE, pages);

	// if (ret < num_pages) {
	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		copyout_copier(dst + offsetNow, vaddr + entry->from_offset + offsetNow, copyLength);
		offsetNow += copyLength;
		if (entry->descriptor_buffer) {
			offsetToWrite = offset + offsetNow - 1;
			*((long *)entry->descriptor_buffer) = offsetToWrite;
		}
		check_sync_task(sync_queue, cp_queue);
		remainingLength -= copyLength;
	}
	// } else {
	// 	while (remainingLength) {
	// 		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
	// 		__memcpy_avx_unaligned(dst + offsetNow, vaddr + entry->from_offset + offsetNow, copyLength);
	// 		offsetNow += copyLength;
	// 		if (entry->descriptor_buffer) {
	// 			offsetToWrite = offset + offsetNow - 1;
	// 			*((long*)entry->descriptor_buffer) = offsetToWrite;
	// 		}
	// 		remainingLength -= copyLength;
	// 	}
	// }

	kunmap(entry->page);
	return 0;
}

static inline int skb_copy_out_multi_task(struct cp_entry *entry)
{
	u8 *vaddr = kmap(entry->page);
	int64_t offsetFromBaseNow;
	int offsetNow = 0, copyLength, remainingLength = entry->length;

	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		offsetFromBaseNow = (unsigned long)entry->to_va - (unsigned long)entry->to_va_base;
		memcpy(entry->to_va_base_kernel + offsetFromBaseNow + offsetNow, vaddr + entry->from_offset + offsetNow, copyLength);
		offsetNow += copyLength;
		if (entry->descriptor_buffer_kernel)
			*(int64_t *)(entry->descriptor_buffer_kernel) = offsetFromBaseNow + offsetNow - 1;
		remainingLength -= copyLength;
	}

	kunmap(entry->page);
	return 0;
}

inline void skb_release(struct cp_entry *entry)
{
	__kfree_skb((struct sk_buff *)entry->skb);
}

long copy_user_to_user(void __user *dst, const void __user *src, unsigned long n)
{
	// printk("copy_user_to_user from=%lx to=%lx len=%lu\n", src, dst, n);
	// unsigned long offset = (unsigned long)dst & (PAGE_SIZE - 1);
	unsigned long bytes;
	long ret = 0;
	int i;
	struct page *pages[128]; //512K at most
	void *page_ptr;

	long num_pages = ((unsigned long)dst + n - ((unsigned long)dst & PAGE_MASK) + PAGE_SIZE - 1) / PAGE_SIZE;
	// pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		return -ENOMEM;
	}

	ret = get_user_pages_fast((unsigned long)dst, num_pages, FOLL_WRITE, pages);

	if (ret < num_pages) {
		copyout_copier(dst, src, n);
	} else {
		__memcpy_avx_unaligned(dst, src, n);
	}

	// for (i = 0; i < num_pages; i++) {
	// 	bytes = min(PAGE_SIZE - offset, n);
	// 	page_ptr = page_to_virt(pages[i]);
	// 	copyout_copier(dst, page_ptr + offset, bytes);
	// 	kunmap(pages[i]);
	// 	dst += bytes;
	// 	n -= bytes;
	// 	offset = 0;
	// }

	// out:
	// kfree(pages);
	return ret;
	// copyin(temp_buffer, src, n);
	// copyout_copier(dst, temp_buffer, n);
	// return n;
}

static inline void copyer_copy_out(struct cp_entry *entry, struct sync_queue *sync_queue, struct cp_queue *cp_queue)
{
	switch (entry->type) {
	case TYPE_RECV_SOCKET_DATA:
		skb_copy_out(entry, sync_queue, cp_queue);
		break;
	case TYPE_SIMPLE_COPY:
		copy_user_to_user(entry->to_va, entry->from_va, entry->length);
		break;
	case TYPE_SOCKET_RELEASE_SKB:
		skb_release(entry);
		break;
	default:
		printk("[copyer] cp_entry type error: %d", entry->type);
	}
}

#define LEASE_PERIOD 4096

static inline bool copyer_copy_out_multi_task(struct cp_entry *entry)
{
	switch (entry->type) {
	case TYPE_RECV_SOCKET_DATA:
		skb_copy_out_multi_task(entry);
		return true;
	case TYPE_SIMPLE_COPY:
		if (entry->length > LEASE_PERIOD) {
			copy_user_to_user(entry->to_va, entry->from_va, LEASE_PERIOD);
			entry->to_va += LEASE_PERIOD;
			entry->from_va += LEASE_PERIOD;
			entry->length -= LEASE_PERIOD;
			return false;
		}
		copy_user_to_user(entry->to_va, entry->from_va, entry->length);
		return true;
	case TYPE_SOCKET_RELEASE_SKB:
		skb_release(entry);
		return true;
	default:
		printk("[copyer] cp_entry type error: %d", entry->type);
	}
	return true;
}

// static inline void process_sync_command(struct sync_entry *sync_entry, struct cp_queue *cp_queue)
// {
// 	// printk("redirect from %lu, length = %lu\n", sync_entry->to_va, sync_entry->length);
// 	int i;
// 	void *redirect_end = sync_entry->to_va + sync_entry->length - 1;
// 	void *cp_entry_end;
// 	unsigned int thread_read_index = cp_queue->thread_read_index, usr_write_index = cp_queue->write_index;
// 	size_t new_length;
// 	void *new_va;
// 	int8_t one = 1;
// 	switch (sync_entry->action) {
// 	case SYNC_COPY_TODO_REDIRECT:
// 		// printk("redirect\n");
// 		for (i = thread_read_index; i < usr_write_index; i++) {
// 			if (cp_queue->entries[i].to_va_base == sync_entry->to_va_base) {
// 				cp_entry_end = cp_queue->entries[i].to_va + cp_queue->entries[i].length - 1;
// 				if (sync_entry->to_va <= cp_queue->entries[i].to_va && redirect_end >= cp_entry_end) {
// 					// printk("true entry at %d\n", i);
// 					cp_queue->entries[i].to_va = cp_queue->entries[i].to_va - cp_queue->entries[i].to_va_base +
// 								     sync_entry->redirect_to_va_offset_offset + sync_entry->redirect_va_base;
// 					cp_queue->entries[i].to_va_base = sync_entry->redirect_va_base;
// 					cp_queue->entries[i].descriptor_buffer = sync_entry->new_descriptor_buffer;
// 				} else if (sync_entry->to_va <= cp_queue->entries[i].to_va && redirect_end < cp_entry_end &&
// 					   redirect_end >= cp_queue->entries[i].to_va) {
// 					// printk("true entry at %d\n", i);
// 					new_va = cp_queue->entries[i].to_va - cp_queue->entries[i].to_va_base +
// 						 sync_entry->redirect_to_va_offset_offset + sync_entry->redirect_va_base;
// 					new_length = redirect_end - new_va + 1;
// 					struct cp_entry remaining_cp = {
// 						.to_va_base = cp_queue->entries[i].to_va_base,
// 						.page = cp_queue->entries[i].page,
// 						.length = cp_queue->entries[i].length - new_length,
// 						.descriptor_buffer = NULL,
// 						.from_offset = cp_queue->entries[i].from_offset + new_length,
// 						.to_va = cp_queue->entries[i].to_va + new_length,
// 					};
// 					skb_copy_out(&remaining_cp);

// 					cp_queue->entries[i].to_va = new_va;
// 					cp_queue->entries[i].to_va_base = sync_entry->redirect_va_base;
// 					cp_queue->entries[i].descriptor_buffer = sync_entry->new_descriptor_buffer;
// 					cp_queue->entries[i].length = new_length;
// 					break;
// 				} else if (sync_entry->to_va > cp_queue->entries[i].to_va && sync_entry->to_va <= cp_entry_end) {
// 					printk("NOT IMPLEMENTED (process_sync_command)\n");
// 				}
// 			}
// 		}
// 		copyout_copier(sync_entry->sync_entry_descriptor, &one, sizeof(int8_t));
// 		break;
// 	default:
// 		printk("NOT IMPLEMENTED CMD(process_sync_command)\n");
// 	}
// }

inline int thread_background_cp(void *ctx_void)
{
	unsigned int thread_read_index, usr_write_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct cp_queue *cp_queue = &ctx->queues_for_recv->queue;
	struct sync_queue *sync_queue = &ctx->queues_for_recv->sync_queue;

	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		check_sync_task(sync_queue, cp_queue);
		{
			thread_read_index = cp_queue->thread_read_index;
			usr_write_index = cp_queue->write_index;

			if (thread_read_index != usr_write_index) {
				copyer_copy_out(&cp_queue->entries[thread_read_index], sync_queue, cp_queue);
				cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
			}
		}
	}
	KTHREAD_DROP_MM(ctx);

	return 0;
}

static int queue_release(struct inode *inode, struct file *filp)
{
	// struct copyer_ctx *ctx = filp->private_data;
	// struct page *page;

	// ctx->should_stop = 1;
	// if (ctx->copyer_thread) {
	// 	kthread_stop(ctx->copyer_thread);
	// 	ctx->copyer_thread = NULL;
	// }
	// vfree(ctx->queue);
	// // kunmap(ctx->sync_queue_page);
	// // free_page((unsigned long)page_to_virt(ctx->sync_queue_page));
	// page = virt_to_head_page(ctx->sync_queue);
	// if (put_page_testzero(page))
	// 	free_compound_page(page);
	// kfree(ctx);
	return 0;
}

// mmap map sync queue into user memory
int queue_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct copyer_ctx *ctx = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->queues_for_recv) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

const struct file_operations queue_fops = {
	.mmap = queue_mmap,
	.release = queue_release,
};

inline void init_cp_queue(struct cp_queue *queue)
{
	int i;
	memset(queue, 0, sizeof(struct cp_queue));
	for (i = 0; i < DEFUALT_CP_ENTRY_NUM; i++) {
		// spin_lock_init(&queue->locks[i]);
		queue->entries[i].status = STATUS_DONE;
	}
	// queue->cp_entry_index_interval_tree_root = RB_ROOT_CACHED;
	// rwlock_init(&queue->interval_tree_lock);
}

inline void init_sync_queue(struct sync_queue *queue)
{
	memset(queue, 0, sizeof(struct sync_queue));
}

inline int recv_prep_cp_thread(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	ctx->queues_for_recv = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	// vmalloc(sizeof(struct cp_queue));
	// ctx->sync_queue_page = alloc_page(GFP_KERNEL);
	// ctx->sync_queue = kmap(ctx->sync_queue_page);
	// ctx->sync_queue = kmalloc(sizeof(struct sync_queue), GFP_KERNEL);
	// gfp = GFP_KERNEL_ACCOUNT | __GFP_ZERO | __GFP_NOWARN | __GFP_COMP;
	// ctx->sync_queue = (void *)__get_free_pages(gfp, get_order(sizeof(struct sync_queue)));
	// if (!ctx->queue)
	// 	printk("fail to alloc cp queue!\n");
	init_cp_queue(&ctx->queues_for_recv->queue);
	init_sync_queue(&ctx->queues_for_recv->sync_queue);
	ctx->thread_func = thread_background_cp;

	file = anon_inode_getfile("[cp_thread_recv]", &queue_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

inline void recv_bind(int userfd, int threadfd)
{
	struct file *f = NULL;
	if (userfd > 0) {
		f = fget(userfd);
		if (f)
			f->queue_fd_out = threadfd;
	}
}

inline void recv_prep_destory(int fd, struct copyer_ctx *ctx)
{
	struct page *page;
	if (fd > 0) {
		struct file *f = fget(fd);
		f->queue_fd_out = -1;
	}
	__free_pages(virt_to_page(ctx->queues_for_recv), get_order(1 << 21));
	// vfree(ctx->queue);
	// kunmap(ctx->sync_queue_page);
	// free_page((unsigned long)page_to_virt(ctx->sync_queue_page));
	// page = virt_to_head_page(ctx->sync_queue);
	// if (put_page_testzero(page))
	// 	free_compound_page(page);
}

/* NEW: for whole system */
int last_visited_cp_queue = 0;
extern struct multi_user_recv_queue_struct multi_user_recv_queue[];
extern struct cp_queue *recv_queue_address_cache[];
extern volatile int recv_queue_last_allocated;

// ROUND ROBIN

inline int pick_up_cp_queue(void)
{
	int i;
	for (i = last_visited_cp_queue + 1; i <= recv_queue_last_allocated; i++)
		if (multi_user_recv_queue[i].valid && (recv_queue_address_cache[i]->thread_read_index != recv_queue_address_cache[i]->write_index)) {
			last_visited_cp_queue = i;
			return i;
		}
	for (i = 0; i <= last_visited_cp_queue; i++)
		if (multi_user_recv_queue[i].valid && (recv_queue_address_cache[i]->thread_read_index != recv_queue_address_cache[i]->write_index)) {
			last_visited_cp_queue = i;
			return i;
		}
	return -1;
}

inline int thread_background_cp_whole_system(void *ctx_void)
{
	struct cp_thread_whole_system_ctx *ctx = (struct cp_thread_whole_system_ctx *)ctx_void;
	unsigned int thread_read_index;
	struct cp_queue *cp_queue;
	int picked_cp_queue;
	struct cp_entry *cp_entry;
	struct mm_struct *current_mm;

	while (!ctx->should_stop) {
		picked_cp_queue = pick_up_cp_queue();
		if (picked_cp_queue == -1)
			continue;

		cp_queue = recv_queue_address_cache[picked_cp_queue];
		thread_read_index = cp_queue->thread_read_index;
		cp_entry = &cp_queue->entries[thread_read_index];
		if (cp_entry->type == TYPE_SIMPLE_COPY && current_mm != multi_user_recv_queue[picked_cp_queue].mm) {
			current_mm = multi_user_recv_queue[picked_cp_queue].mm;
			kthread_use_mm(current_mm);
		}
		if (copyer_copy_out_multi_task(cp_entry))
			cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
	}
	return 0;
}
