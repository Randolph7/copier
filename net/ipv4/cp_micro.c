#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

static int u2u_queue_release(struct inode *inode, struct file *filp)
{
	return 0;
}

// mmap map sync queue into user memory
static int u2u_queue_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct copyer_ctx *ctx = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->queues_for_u2u) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

const struct file_operations u2u_queue_fops = {
	.mmap = u2u_queue_mmap,
	.release = u2u_queue_release,
};

static inline void u2u_copy_user_to_user_core(const void *src, const void *dst, const void *base, const size_t n,
					    volatile uint16_t *descriptors, const int granularity)
{
	const long offset_to_base = dst - base;
	long offset = 0;
	// unsigned long page_start = (unsigned long)src & PAGE_MASK;
	// long ret = 0;
	// struct page **pages;
	// void *kernel_map_base, *kmap_addr;
	uint16_t cp_size;
	size_t bytes = n;

	// unsigned long num_pages = ((unsigned long)src + n - page_start + PAGE_SIZE - 1) / PAGE_SIZE;
	// pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	// if (!pages)
	// 	return;

	// ret = get_user_pages_fast(page_start, num_pages, 1, pages);

	// if (ret < 0)
	// 	goto out;

	// kmap_addr = vmap(pages, num_pages, VM_MAP, PAGE_KERNEL);
	// kernel_map_base = kmap_addr + (unsigned long)src % PAGE_SIZE;

		while (bytes) {
			cp_size = bytes > granularity ? granularity : bytes;
			// copyout_copier((void *)(dst + offset), kernel_map_base + offset, cp_size);
			memcpy((void *)(dst + offset), src + offset, cp_size);
			*(descriptors + (offset_to_base + offset) / granularity) = cp_size;
			offset += cp_size;
			bytes -= cp_size;
		}
	// vunmap(kmap_addr);
	// out:
	// 	kfree(pages);
}

extern size_t __memcpy_avx_unaligned(void *dest, const void *src, size_t n);

static inline void u2u_copy_user_to_user_core_short(const void *src, const void *dst, const void *base, const uint16_t n,
						    volatile uint16_t *descriptors, const int granularity)
{
	const long offset_to_base = dst - base;
	// unsigned long page_start = (unsigned long)src & PAGE_MASK;
	// long ret = 0;
	// struct page **pages;
	// void *kernel_map_base, *kmap_addr;

	// unsigned long num_pages = ((unsigned long)src + n - page_start + PAGE_SIZE - 1) / PAGE_SIZE;
	// pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	// if (!pages)
	// 	return;

	// ret = get_user_pages_fast(page_start, num_pages, 1, pages);

	// if (ret < 0)
	// 	goto out;

	// kmap_addr = vmap(pages, num_pages, VM_MAP, PAGE_KERNEL);
	// kernel_map_base = kmap_addr + (unsigned long)src % PAGE_SIZE;

	// copyout_copier((void *)dst, kernel_map_base, n);
	__memcpy_avx_unaligned((void *)dst, src, n);
	*(descriptors + offset_to_base / granularity) = n;

	// 	vunmap(kmap_addr);
	// out:
	// 	kfree(pages);
}

static inline bool u2u_copy_user_to_user(struct u2u_cp_entry *entry)
{
	bool finish = false;
	const int granularity = entry->granularity;
	const unsigned long size = entry->size;
	unsigned long offset, lifo_cp_size;
		if (size > granularity) {
			u2u_copy_user_to_user_core_short(entry->from, entry->to, entry->base, granularity, entry->descriptors, granularity);
			entry->from += granularity;
			entry->to += granularity;
			entry->size -= granularity;
		} else {
			u2u_copy_user_to_user_core_short(entry->from, entry->to, entry->base, size, entry->descriptors, granularity);
			entry->status = STATUS_DONE;
			finish = true;
		}
	
	return finish;

	// u2u_copy_user_to_user_core(entry->from, entry->to, entry->base, entry->size, entry->descriptors, entry->granularity, entry->lifo);
	// entry->status = STATUS_DONE;
	// return true;
}

#define ENTRY_OVERLAP_FRONT(cp_entry, start_addr, end_addr) (end_addr >= cp_entry->to && start_addr <= cp_entry->to)
#define ENTRY_OVERLAP_MIDDLE(cp_entry, start_addr, end_addr, cp_entry_end) (start_addr >= cp_entry->to && end_addr <= cp_entry_end)
#define ENTRY_OVERLAP_END(cp_entry, start_addr, end_addr, cp_entry_end) (end_addr >= cp_entry_end && start_addr <= cp_entry_end)
#define U2U_ALIGN_DOWN(a, granularity) (a) / (granularity) * (granularity)
#define U2U_ALIGN_UP(a, granularity) (a + granularity - 1) / (granularity) * (granularity)

static void u2u_process_sync_entry(struct u2u_sync_entry *sync_entry, struct u2u_cp_queue *cp_queue)
{
	// printk("u2u_process_sync_entry\n");
	void *sync_start_addr = sync_entry->start_addr, *sync_end_addr = sync_entry->start_addr + sync_entry->size, *base;
	const size_t size = sync_entry->size;
	int thread_read_index = cp_queue->thread_read_index, write_index = cp_queue->write_index, granularity;
	struct u2u_cp_entry *cp_entry;
	void *sync_cp_start_addr, *sync_cp_end_addr, *cp_entry_end;
	long cp_size;
	int i;

	for (i = thread_read_index; i < write_index; i++) {
		// printk("%d\n", 123);
		cp_entry = &cp_queue->entries[i];
		if (cp_entry->status == STATUS_WAITING) {
			cp_entry_end = cp_entry->to + cp_entry->size;
			granularity = cp_entry->granularity;
			base = cp_entry->base;
			if (ENTRY_OVERLAP_END(cp_entry, sync_start_addr, sync_end_addr, cp_entry_end)) {
				// printk("tail\n");
				sync_cp_start_addr = U2U_ALIGN_DOWN(sync_start_addr - base, granularity) + base;
				if (unlikely(cp_entry->to > sync_cp_start_addr))
					sync_cp_start_addr = cp_entry->to;
				cp_size = sync_end_addr - sync_cp_start_addr;
				u2u_copy_user_to_user_core(cp_entry->from + (unsigned long)sync_cp_start_addr - (unsigned long)cp_entry->to,
							   sync_cp_start_addr, base, cp_size, cp_entry->descriptors, granularity);
				cp_entry->size -= cp_size;
				if (unlikely(!cp_entry->size))
					cp_entry->status = STATUS_DONE;
				if (likely(sync_end_addr == cp_entry_end))
					break;
			}
		}
	}
}

inline int u2u_thread_background_cp(void *ctx_void)
{
	unsigned int thread_read_index, usr_write_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct u2u_cp_queue *cp_queue = &ctx->queues_for_u2u->cp_queue;
	struct u2u_sync_queue *sync_queue = &ctx->queues_for_u2u->sync_queue;

	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		while (sync_queue->thread_read_index != sync_queue->write_index) {
			u2u_process_sync_entry(&sync_queue->entries[sync_queue->thread_read_index], cp_queue);
			sync_queue->thread_read_index = (sync_queue->thread_read_index + 1) % DEFUALT_SYNC_ENTRY_NUM;
		}
		{
			thread_read_index = cp_queue->thread_read_index;
			usr_write_index = cp_queue->write_index;

			if (thread_read_index != usr_write_index) {
				if (cp_queue->entries[thread_read_index].status == STATUS_WAITING)
					if (u2u_copy_user_to_user(&cp_queue->entries[thread_read_index]))
						cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
			}
		}
	}
	KTHREAD_DROP_MM(ctx);

	return 0;
}

inline int u2u_prep_cp_thread(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	int i;

	ctx->queues_for_u2u = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	memset(ctx->queues_for_u2u, 0, sizeof(struct queues_for_u2u));
	for (i = 0; i < DEFUALT_CP_ENTRY_NUM; i++)
		ctx->queues_for_u2u->cp_queue.entries[i].status = STATUS_DONE;
	ctx->thread_func = u2u_thread_background_cp;

	file = anon_inode_getfile("[cp_thread_u2u]", &u2u_queue_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
};

inline void u2u_bind(int userfd, int threadfd)
{
	struct file *f = NULL;
	if (userfd > 0) {
		f = fget(userfd);
		if (f)
			f->queue_fd_u2u = threadfd;
	}
}

inline void u2u_prep_destory(int fd, struct copyer_ctx *ctx)
{
	if (fd > 0) {
		struct file *f = fget(fd);
		f->queue_fd_u2u = -1;
	}
	__free_pages(virt_to_page(ctx->queues_for_u2u), get_order(1 << 21));
}
