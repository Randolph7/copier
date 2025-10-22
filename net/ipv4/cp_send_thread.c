#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>
// #include "cp_memlog.h"

extern size_t __memcpy_avx_unaligned(void *dest, const void *src, size_t n);

static int empty_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int queue_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct copyer_ctx *ctx = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->queue_in) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

const struct file_operations empty_fops = {
	.release = empty_release,
	.mmap = queue_mmap,
};

inline void init_cp_in_queue(struct cp_in_queue *queue)
{
	int i;
	memset(queue, 0, sizeof(struct cp_in_queue));
	for (i = 0; i < DEFUALT_CP_IN_ENTRY_NUM; i++) {
		queue->entries[i].status = STATUS_DONE;
	}
	queue->descriptor_index = 0;
	hash_init(queue->ht);
	queue->hash_node_index = 0;
}

extern long copy_user_to_user(void __user *dst, const void __user *src, unsigned long n);

inline int thread_background_cp_in(void *ctx_void)
{
	unsigned int thread_read_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct cp_in_queue *cp_queue = ctx->queue_in;
	struct cp_in_entry *entry;

	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		thread_read_index = cp_queue->thread_read_index;
		if (thread_read_index != cp_queue->write_index) {
			entry = &cp_queue->entries[thread_read_index];
			if (entry->type == TYPE_SIMPLE_COPY)
				copy_user_to_user(entry->to_va, entry->from_va, entry->length);
			else {
				__memcpy_avx_unaligned(entry->to_va, entry->from_va, entry->length);
				*(entry->descriptor) = COPIER_BLOCK_DONE;
				entry->status = STATUS_DONE;
			}
			cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;
		}
	}

	KTHREAD_DROP_MM(ctx);
	return 0;
}

extern struct cp_queue lazy_cp_queue;
extern inline void skb_release(struct cp_entry *entry);

// only simple implement
inline void lazy_copy_send_data_kq(void *to_va, void *from_va, long length)
{
	u64 from_va_end = (u64)from_va + length;
	u64 to_va_end = (u64)to_va + length;
	void *current_to_va = to_va;
	unsigned int thread_read_index;
	unsigned int write_index;
	int i;
	struct cp_entry *entry;
	u64 entry_end;
	u8 *vaddr;
	bool to_end = false;
	long length_last;
	u64 from_va_current = (u64)from_va;

	thread_read_index = lazy_cp_queue.thread_read_index;
	write_index = lazy_cp_queue.write_index;
	for (i = thread_read_index; i != write_index; i = (i + 1) % DEFUALT_CP_ENTRY_NUM) {
		entry = &lazy_cp_queue.entries[i];
		if (entry->type == TYPE_SOCKET_RELEASE_SKB) {
			skb_release(entry);
			lazy_cp_queue.thread_read_index = (i + 1) % DEFUALT_CP_ENTRY_NUM;
			if (to_end)
				break;
		} else {
			if (to_end)
				break;
			entry_end = (u64)entry->to_va + entry->length;
			if (entry_end <= from_va_end) {
				if ((u64)entry->to_va != from_va_current)
					printk("some error in lazy send full\n");
				else {
					vaddr = kmap(entry->page);
					__memcpy_avx_unaligned(current_to_va, vaddr + entry->from_offset, entry->length);
					kunmap(entry->page);
					current_to_va += entry->length;
					if ((u64)current_to_va == to_va_end)
						to_end = true;
					from_va_current += entry->length;
				}
				lazy_cp_queue.thread_read_index = (i + 1) % DEFUALT_CP_ENTRY_NUM;
			} else {
				if ((u64)entry->to_va != from_va_current)
					printk("some error in lazy send\n");
				else {
					vaddr = kmap(entry->page);
					length_last = to_va_end - (u64)current_to_va;
					__memcpy_avx_unaligned(current_to_va, vaddr + entry->from_offset, length_last);
					kunmap(entry->page);
					entry->from_offset += length_last;
					entry->length -= length_last;
					entry->to_va += length_last;
					break;
				}
			}
		}
	}
}

inline void lazy_copy_send_data(struct cp_in_queue *uq, void *to_va, void *from_va, long length)
{
	unsigned int thread_read_index;
	struct cp_in_entry *entry;

	thread_read_index = uq->thread_read_index;
	entry = &uq->entries[thread_read_index];
	if (entry->to_va != from_va)
		printk("some error in ucp absorb, entry->to_va = %lu, length = %ld\n", entry->to_va, length);

	lazy_copy_send_data_kq(to_va, entry->from_va, length);
	if (entry->length == length)
		uq->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
	else {
		entry->length -= length;
		entry->from_va += length;
		entry->to_va += length;
	}
}

inline int thread_background_cp_in_lazy(void *ctx_void)
{
	unsigned int thread_read_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct cp_in_queue *cp_queue = ctx->queue_in;
	struct cp_in_queue *cp_queue2 = (struct cp_in_queue *)((void *)ctx->queue_in - sizeof(struct cp_in_queue));
	struct cp_in_entry *entry;

	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		thread_read_index = cp_queue->thread_read_index;
		if (thread_read_index != cp_queue->write_index) {
			entry = &cp_queue->entries[thread_read_index];
			lazy_copy_send_data(cp_queue2, entry->to_va, entry->from_va, entry->length);
			*(entry->descriptor) = COPIER_BLOCK_DONE;
			entry->status = STATUS_DONE;

			cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;
		}
	}

	KTHREAD_DROP_MM(ctx);
	return 0;
}

/* NEW: for whole system */
int last_visited_cp_in_queue = 0;
extern struct multi_user_send_queue_struct multi_user_send_queue[];
extern struct cp_in_queue *send_queue_address_cache[];
extern volatile int send_queue_last_allocated;

// ROUND ROBIN

inline int pick_up_cp_in_queue(void)
{
	int i;
	for (i = last_visited_cp_in_queue + 1; i <= send_queue_last_allocated; i++)
		if (multi_user_send_queue[i].valid && (send_queue_address_cache[i]->thread_read_index != send_queue_address_cache[i]->write_index)) {
			last_visited_cp_in_queue = i;
			return i;
		}
	for (i = 0; i <= last_visited_cp_in_queue; i++)
		if (multi_user_send_queue[i].valid && (send_queue_address_cache[i]->thread_read_index != send_queue_address_cache[i]->write_index)) {
			last_visited_cp_in_queue = i;
			return i;
		}
	return -1;
}

inline int thread_background_cp_in_whole_system(void *ctx_void)
{
	struct cp_thread_whole_system_ctx *ctx = (struct cp_thread_whole_system_ctx *)ctx_void;
	unsigned int thread_read_index;
	struct cp_in_queue *cp_queue;
	int picked_cp_queue;

	while (!ctx->should_stop) {
		picked_cp_queue = pick_up_cp_in_queue();
		if (picked_cp_queue == -1)
			continue;

		cp_queue = send_queue_address_cache[picked_cp_queue];
		thread_read_index = cp_queue->thread_read_index;
		memcpy(cp_queue->entries[thread_read_index].to_va, cp_queue->entries[thread_read_index].from_va_kernel,
		       cp_queue->entries[thread_read_index].length);
		*(cp_queue->entries[thread_read_index].descriptor) = COPIER_BLOCK_DONE;
		cp_queue->entries[thread_read_index].status = STATUS_DONE;
		cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;
	}
	return 0;
}

inline int send_prep_cp_thread(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	ctx->queue_in = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	if (!ctx->queue_in)
		printk("fail to alloc cp in queue!\n");
	init_cp_in_queue(ctx->queue_in);
	ctx->thread_func = thread_background_cp_in;
	file = anon_inode_getfile("[cp_thread_send]", &empty_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

inline int send_prep_cp_thread_lazy(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	ctx->queue_in = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	if (!ctx->queue_in)
		printk("fail to alloc cp in queue!\n");
	init_cp_in_queue(ctx->queue_in);
	ctx->queue_in = (struct cp_in_queue *)((void *)ctx->queue_in + sizeof(struct cp_in_queue));
	init_cp_in_queue(ctx->queue_in);
	ctx->thread_func = thread_background_cp_in_lazy;
	file = anon_inode_getfile("[cp_thread_send]", &empty_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

inline void send_bind(int userfd, int threadfd)
{
	struct file *f = NULL;
	if (userfd > 0) {
		f = fget(userfd);
		if (f)
			f->queue_fd_in = threadfd;
	}
}

inline void send_prep_destory(int fd, struct copyer_ctx *ctx)
{
	if (fd > 0) {
		struct file *f = fget(fd);
		f->queue_fd_in = -1;
	}
	kfree(ctx->queue_in);
}
