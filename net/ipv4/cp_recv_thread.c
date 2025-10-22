#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>
#include "cp_memlog.h"

INTERVAL_TREE_DEFINE(struct cp_entry_interval_tree_node, rb, unsigned long, __subtree_last, START, LAST, , cp_entry_interval_tree)

static inline int skb_copy_out(struct cp_entry *entry)
{
	u8 *vaddr = kmap(entry->page);
#ifdef MULTI_USER
	memcpy(entry->to_va_base_kernel + entry->to_va_offset, vaddr + entry->from_offset, entry->length);
#else
	copyout(entry->to_va_base + entry->to_va_offset, vaddr + entry->from_offset, entry->length);
#endif
	kunmap(entry->page);
	return 0;
}

inline struct cp_entry_interval_tree_node *add_interval_to_tree(struct cp_queue *queue, long start, int length, long entry_index)
{
	struct cp_entry_interval_tree_node *inter_tree_node = kmalloc(sizeof(struct cp_entry_interval_tree_node), GFP_KERNEL);
	inter_tree_node->start = start;
	inter_tree_node->last = start + length - 1;
	inter_tree_node->entry_index_in_queue = entry_index;
	write_lock(&queue->interval_tree_lock);
	cp_entry_interval_tree_insert(inter_tree_node, &queue->cp_entry_index_interval_tree_root);
	write_unlock(&queue->interval_tree_lock);
	return inter_tree_node;
}

inline void delete_interval_from_tree(struct cp_queue *queue, struct cp_entry_interval_tree_node *node)
{
	write_lock(&queue->interval_tree_lock);
	// if (!node)
	// 	printk("node == NULL!\n");
	// else
	// 	printk("node->start = %d, ->last = %d, addr = %ld\n", node->start, node->last, (unsigned long)node);
	cp_entry_interval_tree_remove(node, &queue->cp_entry_index_interval_tree_root);
	write_unlock(&queue->interval_tree_lock);
	kfree(node);
}
#ifdef MULTI_USER
static inline void set_copy_finish_bytes_kernel(void *descriptor_buffer, int to_va_offset, descriptor_entry byte)
{
	if (descriptor_buffer)
		memcpy(GET_DESCRIPTOR_ENTRY_ADDRESS(descriptor_buffer, to_va_offset), &byte, sizeof(descriptor_entry));
	// printk("set %ld %d\n", (unsigned long)GET_DESCRIPTOR_ENTRY_ADDRESS(descriptor_buffer, to_va_offset), byte);
}
#else
/* Note：Assume that a block is divided into two parts at most */
static inline void set_copy_finish_bytes(void __user *descriptor_buffer, int to_va_offset, descriptor_entry byte)
{
	if (descriptor_buffer)
		copyout(GET_DESCRIPTOR_ENTRY_ADDRESS(descriptor_buffer, to_va_offset), &byte, sizeof(descriptor_entry));
	// printk("set %ld %d\n", (unsigned long)GET_DESCRIPTOR_ENTRY_ADDRESS(descriptor_buffer, to_va_offset), byte);
}
#endif

static inline void skb_release(struct cp_entry *entry)
{
	__kfree_skb((struct sk_buff *)entry->skb);
}

static inline void copyer_copy_out(struct cp_queue *queue, struct cp_entry *entry, struct cp_entry *next_entry)
{
	// printk("type = %d\n", entry->type);
	switch (entry->type) {
	case TYPE_RECV_SOCKET_DATA:
		skb_copy_out(entry);
#ifdef MULTI_USER
		set_copy_finish_bytes_kernel(entry->descriptor_buffer_kernel, entry->to_va_offset, COPIER_BLOCK_DONE);
#else
		set_copy_finish_bytes(entry->descriptor_buffer, entry->to_va_offset, COPIER_BLOCK_DONE);
#endif
		// printk("single block, from %lu to %lu, block %d\n", (unsigned long)entry->to_va_offset,
		//        (unsigned long)entry->to_va_offset + (unsigned long)entry->length - 1, GET_BLOCK_N(entry->to_va_offset));
		delete_interval_from_tree(queue, entry->interval_tree_node);
		break;
	case TYPE_RECV_SOCKET_DATA_TWO_BLOCK:
		skb_copy_out(entry);
		skb_copy_out(next_entry);
		if (GET_BLOCK_N(entry->to_va_offset) != GET_BLOCK_N(entry->to_va_offset + next_entry->length + entry->length - 1)) {
#ifdef MULTI_USER
			set_copy_finish_bytes_kernel(entry->descriptor_buffer_kernel, entry->to_va_offset, COPIER_BLOCK_DONE);
			set_copy_finish_bytes_kernel(entry->descriptor_buffer_kernel, entry->to_va_offset + next_entry->length + entry->length - 1,
						     COPIER_BLOCK_DONE);
#else
			set_copy_finish_bytes(entry->descriptor_buffer, entry->to_va_offset, COPIER_BLOCK_DONE);
			set_copy_finish_bytes(entry->descriptor_buffer, entry->to_va_offset + next_entry->length + entry->length - 1,
					      COPIER_BLOCK_DONE);
#endif
		} else {
#ifdef MULTI_USER
			set_copy_finish_bytes_kernel(entry->descriptor_buffer_kernel, entry->to_va_offset, COPIER_BLOCK_DONE);
#else
			set_copy_finish_bytes(entry->descriptor_buffer, entry->to_va_offset, COPIER_BLOCK_DONE);
#endif
		}
		// printk("two block, from %lu to %lu, block %d\n", entry->to_va_offset,
		//        entry->to_va_offset + next_entry->length + entry->length - 1, GET_BLOCK_N(entry->to_va_offset));
		delete_interval_from_tree(queue, entry->interval_tree_node);
		break;
	case TYPE_SOCKET_RELEASE_SKB:
		skb_release(entry);
		break;
#ifdef BREAKDOWN
	case TYPE_BREAKDOWN_LOG:
		add_copier_finish_log_to_queue(entry->io_length);
		break;
#endif
	default:
		printk("[copyer] cp_entry type error: %d", entry->type);
	}
}

static inline void find_cp_entries_by_interval(struct cp_queue *queue, struct sync_entry *sync_entry,
					       int (*action)(struct cp_queue *queue, long cp_entry_index, long next_entry_index,
							     struct sync_entry *sync_entry, struct cp_entry_interval_tree_node *node))
{
	unsigned long start = (long)sync_entry->to_va_base + sync_entry->to_va_offset;
	unsigned long last = start + sync_entry->length - 1;
	struct cp_entry_interval_tree_node *node;

	// read_lock(&queue->interval_tree_lock);
	node = cp_entry_interval_tree_iter_first(&queue->cp_entry_index_interval_tree_root, start, last);
	while (node) {
		if (queue->entries[node->entry_index_in_queue].status != STATUS_DONE) {
			if (queue->entries[node->entry_index_in_queue].type == TYPE_RECV_SOCKET_DATA) {
				// printk("index %d is TYPE_RECV_SOCKET_DATA\n", node->entry_index_in_queue);
				action(queue, node->entry_index_in_queue, -1, sync_entry, node);
			} else if (queue->entries[node->entry_index_in_queue].type == TYPE_RECV_SOCKET_DATA_TWO_BLOCK) {
				// printk("index %d is TYPE_RECV_SOCKET_DATA_TWO_BLOCK, next index = %d\n", node->entry_index_in_queue,
				//    (node->entry_index_in_queue + 1) % DEFUALT_CP_ENTRY_NUM);
				action(queue, node->entry_index_in_queue, (node->entry_index_in_queue + 1) % DEFUALT_CP_ENTRY_NUM, sync_entry, node);
			}
		}
		node = cp_entry_interval_tree_iter_first(&queue->cp_entry_index_interval_tree_root, start, last);
	}

	// for (node = cp_entry_interval_tree_iter_first(&queue->cp_entry_index_interval_tree_root, start, last); node;
	//      node = cp_entry_interval_tree_iter_next(node, start, last))
	// 	if (queue->entries[node->entry_index_in_queue].status != STATUS_DONE) {
	// 		if (queue->entries[node->entry_index_in_queue].type == TYPE_RECV_SOCKET_DATA)
	// 			action(queue, node->entry_index_in_queue, 0, sync_entry);
	// 		else if (queue->entries[node->entry_index_in_queue].type == TYPE_RECV_SOCKET_DATA_TWO_BLOCK)
	// 			action(queue, node->entry_index_in_queue, (node->entry_index_in_queue + 1) % DEFUALT_CP_ENTRY_NUM, sync_entry);
	// 	}
	// read_unlock(&queue->interval_tree_lock);
}

static inline int sync_skb_copy_out(struct cp_queue *queue, long cp_entry_index, long next_entry_index, struct sync_entry *sync_entry,
				    struct cp_entry_interval_tree_node *node)
{
	struct cp_entry *cp_entry = &queue->entries[cp_entry_index];
	// printk("sync_skb_copy_out, from %d\n", (unsigned long)cp_entry->to_va_offset + (unsigned long)cp_entry->to_va_base);
	UNUSED(sync_entry);
	skb_copy_out(cp_entry);
	cp_entry->status = STATUS_DONE;
	// printk("set index %d DONE\n", cp_entry_index);
	if (next_entry_index >= 0) {
		struct cp_entry *next_entry = &queue->entries[next_entry_index];
		skb_copy_out(next_entry);
		next_entry->status = STATUS_DONE;
		// printk("set index %d DONE\n", next_entry_index);
	}
#ifdef MULTI_USER
	set_copy_finish_bytes_kernel(cp_entry->descriptor_buffer_kernel, cp_entry->to_va_offset, COPIER_BLOCK_DONE);
#else
	set_copy_finish_bytes(cp_entry->descriptor_buffer, cp_entry->to_va_offset, COPIER_BLOCK_DONE);
#endif
	// printk("sync single block, from %d to %d\n", cp_entry->interval_tree_node->start, cp_entry->interval_tree_node->last);
	delete_interval_from_tree(queue, node);
	return 0;
}

/* redirect_cp_entry: currently only supports 1k granularity */
static inline int redirect_cp_entry(struct cp_queue *queue, long cp_entry_index, long next_entry_index, struct sync_entry *sync_entry,
				    struct cp_entry_interval_tree_node *node)
{
	// printk("redirect_cp_entry to_va_offset = %d, len = %d, sync_entry->to_va_offset = %d, sync_entry->length = %d\n",
	//        queue->entries[cp_entry_index].to_va_offset, queue->entries[cp_entry_index].length, sync_entry->to_va_offset, sync_entry->length);
	void *prev_descriptor_buffer;
	int prev_to_va_offset;
	struct cp_entry *cp_entry = &queue->entries[cp_entry_index];
	// printk("redirect_cp_entry, from %d\n", (unsigned long)cp_entry->to_va_offset + (unsigned long)cp_entry->to_va_base);
#ifdef MULTI_USER
	prev_descriptor_buffer = cp_entry->descriptor_buffer_kernel;
#else
	prev_descriptor_buffer = cp_entry->descriptor_buffer;
#endif
	prev_to_va_offset = cp_entry->to_va_offset;

	cp_entry->to_va_base = sync_entry->redirect_va;
	cp_entry->to_va_offset += sync_entry->redirect_to_va_offset_offset;
	cp_entry->descriptor_buffer = sync_entry->new_descriptor_buffer;
	delete_interval_from_tree(queue, node);
	if (next_entry_index >= 0) {
		struct cp_entry *next_entry = &queue->entries[next_entry_index];
		next_entry->to_va_base = sync_entry->redirect_va;
		next_entry->to_va_offset += sync_entry->redirect_to_va_offset_offset;
		next_entry->descriptor_buffer = sync_entry->new_descriptor_buffer;
		cp_entry->interval_tree_node = add_interval_to_tree(queue, (unsigned long)cp_entry->to_va_base + cp_entry->to_va_offset,
								    cp_entry->length + next_entry->length, cp_entry_index);
	} else
		cp_entry->interval_tree_node =
			add_interval_to_tree(queue, (unsigned long)cp_entry->to_va_base + cp_entry->to_va_offset, cp_entry->length, cp_entry_index);
#ifdef MULTI_USER
	set_copy_finish_bytes_kernel(prev_descriptor_buffer, prev_to_va_offset, COPIER_BLOCK_REDIRECTED);
#else
	set_copy_finish_bytes(prev_descriptor_buffer, prev_to_va_offset, COPIER_BLOCK_REDIRECTED);
#endif
	return 0;
}

#define ENTRY_START(entry) ((long)entry->to_va_base + entry->to_va_offset)
#define ENTRY_END(entry) ((long)entry->to_va_base + entry->to_va_offset + entry->length)

static inline void copy_start_and_end(struct cp_entry *cp_entry, struct sync_entry *sync_entry)
{
	long front_offset, end_offset;
	front_offset = ENTRY_START(cp_entry) - ENTRY_START(sync_entry);
	end_offset = ENTRY_END(cp_entry) - ENTRY_END(sync_entry);
	if (front_offset < 0) {
		struct cp_entry cp_entry_front = {
			.page = cp_entry->page,
			.to_va_base = cp_entry->to_va_base,
			.to_va_offset = cp_entry->to_va_offset,
			.from_offset = cp_entry->from_offset,
			.length = -front_offset,
		};
		skb_copy_out(&cp_entry_front);
	}
	if (end_offset > 0) {
		struct cp_entry cp_entry_front = {
			.page = cp_entry->page,
			.to_va_base = sync_entry->to_va_base,
			.to_va_offset = sync_entry->to_va_offset + sync_entry->length,
			.from_offset = cp_entry->from_offset + (sync_entry->to_va_offset + sync_entry->length - cp_entry->to_va_offset),
			.length = end_offset,
		};
		skb_copy_out(&cp_entry_front);
	}
}

static inline int abort_cp_entry(struct cp_queue *queue, long cp_entry_index, long next_entry_index, struct sync_entry *sync_entry,
				 struct cp_entry_interval_tree_node *node)
{
	struct cp_entry *cp_entry = &queue->entries[cp_entry_index];
	copy_start_and_end(cp_entry, sync_entry);
	cp_entry->status = STATUS_DONE;

	if (next_entry_index >= 0) {
		struct cp_entry *next_entry = &queue->entries[next_entry_index];
		copy_start_and_end(next_entry, sync_entry);
		next_entry->status = STATUS_DONE;
	}
#ifdef MULTI_USER
	set_copy_finish_bytes_kernel(cp_entry->descriptor_buffer_kernel, cp_entry->to_va_offset, COPIER_BLOCK_DONE);
#else
	set_copy_finish_bytes(cp_entry->descriptor_buffer, cp_entry->to_va_offset, COPIER_BLOCK_DONE);
#endif
	delete_interval_from_tree(queue, node);
	return 0;
}

static inline void process_sync_calls(struct cp_queue *queue, struct sync_entry *entry)
{
	// printk("process_sync_calls action = %d\n", entry->action);
	switch (entry->action) {
	case SYNC_COPY_TODO_ADVANCE:
		find_cp_entries_by_interval(queue, entry, sync_skb_copy_out);
		break;
	case SYNC_COPY_TODO_ABORT:
		find_cp_entries_by_interval(queue, entry, abort_cp_entry);
		break;
	case SYNC_COPY_TODO_REDIRECT:
		find_cp_entries_by_interval(queue, entry, redirect_cp_entry);
		break;
	default:
		return;
	}
	// printk("finish this sync call\n");
}

inline int thread_background_cp(void *ctx_void)
{
	unsigned int thread_read_index, usr_write_index;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct cp_queue *cp_queue = ctx->queue;
	struct sync_queue *sync_queue = ctx->sync_queue;
	bool consume_two_entry = false;
	unsigned int next_entry_index = 0;

	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		thread_read_index = cp_queue->thread_read_index;
		usr_write_index = cp_queue->write_index;
		consume_two_entry = false;
		if (thread_read_index != usr_write_index) {
			if (cp_queue->entries[thread_read_index].status == STATUS_WAITING) {
				if (cp_queue->entries[thread_read_index].type == TYPE_RECV_SOCKET_DATA_TWO_BLOCK) {
					consume_two_entry = true;
					next_entry_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
					copyer_copy_out(cp_queue, &cp_queue->entries[thread_read_index], &cp_queue->entries[next_entry_index]);
					cp_queue->entries[thread_read_index].status = STATUS_DONE;
					cp_queue->entries[next_entry_index].status = STATUS_DONE;
					// printk("finish index %d and %d\n", thread_read_index, next_entry_index);
				} else {
					copyer_copy_out(cp_queue, &cp_queue->entries[thread_read_index], NULL);
					cp_queue->entries[thread_read_index].status = STATUS_DONE;
					// printk("finish index %d\n", thread_read_index);
				}
			} else {
				// printk("index %d is done\n", thread_read_index);
			}

			if (consume_two_entry)
				cp_queue->thread_read_index = (next_entry_index + 1) % DEFUALT_CP_ENTRY_NUM;
			else
				cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
		}

		while (sync_queue->thread_read_index != sync_queue->write_index) {
			// printk("find a sync entry\n");
			process_sync_calls(cp_queue, &sync_queue->entries[sync_queue->thread_read_index]);
			sync_queue->entries[sync_queue->thread_read_index].status = STATUS_DONE;
			sync_queue->thread_read_index = (sync_queue->thread_read_index + 1) % DEFUALT_SYNC_ENTRY_NUM;
			// printk("finish a sync entry\n");
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
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->sync_queue) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
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
	queue->cp_entry_index_interval_tree_root = RB_ROOT_CACHED;
	rwlock_init(&queue->interval_tree_lock);
}

inline void init_sync_queue(struct sync_queue *queue)
{
	memset(queue, 0, sizeof(struct sync_queue));
	queue->size = DEFUALT_SYNC_ENTRY_NUM;
}

inline int recv_prep_cp_thread(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	gfp_t gfp;
	ctx->queue = vmalloc(sizeof(struct cp_queue));
	// ctx->sync_queue_page = alloc_page(GFP_KERNEL);
	// ctx->sync_queue = kmap(ctx->sync_queue_page);
	// ctx->sync_queue = kmalloc(sizeof(struct sync_queue), GFP_KERNEL);
	gfp = GFP_KERNEL_ACCOUNT | __GFP_ZERO | __GFP_NOWARN | __GFP_COMP;
	ctx->sync_queue = (void *)__get_free_pages(gfp, get_order(sizeof(struct sync_queue)));
	if (!ctx->queue)
		printk("fail to alloc cp queue!\n");
	init_cp_queue(ctx->queue);
	init_sync_queue(ctx->sync_queue);
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
	vfree(ctx->queue);
	// kunmap(ctx->sync_queue_page);
	// free_page((unsigned long)page_to_virt(ctx->sync_queue_page));
	page = virt_to_head_page(ctx->sync_queue);
	if (put_page_testzero(page))
		free_compound_page(page);
}

/* NEW: for whole system */
int last_visited_sync_queue = 0, last_visited_cp_queue = 0;
extern struct multi_user_recv_queue_struct multi_user_recv_queue[];
extern volatile int last_allocated;

// ROUND ROBIN

inline int pick_up_sync_queue(void)
{
	int i;
	for (i = last_visited_sync_queue + 1; i <= last_allocated; i++)
		if (multi_user_recv_queue[i].valid &&
		    (multi_user_recv_queue[i].sync_queue->thread_read_index != multi_user_recv_queue[i].sync_queue->write_index)) {
			last_visited_sync_queue = i;
			return i;
		}
	for (i = 0; i <= last_visited_sync_queue; i++)
		if (multi_user_recv_queue[i].valid &&
		    (multi_user_recv_queue[i].sync_queue->thread_read_index != multi_user_recv_queue[i].sync_queue->write_index)) {
			last_visited_sync_queue = i;
			return i;
		}
	return -1;
}

inline int pick_up_cp_queue(void)
{
	int i;
	for (i = last_visited_cp_queue + 1; i <= last_allocated; i++)
		if (multi_user_recv_queue[i].valid &&
		    (multi_user_recv_queue[i].queue.thread_read_index != multi_user_recv_queue[i].queue.write_index)) {
			last_visited_cp_queue = i;
			return i;
		}
	for (i = 0; i <= last_visited_cp_queue; i++)
		if (multi_user_recv_queue[i].valid &&
		    (multi_user_recv_queue[i].queue.thread_read_index != multi_user_recv_queue[i].queue.write_index)) {
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
	struct sync_queue *sync_queue;
	bool consume_two_entry = false;
	unsigned int next_entry_index = 0;
	int picked_sync_queue, picked_cp_queue;

	while (!ctx->should_stop) {
		{
			picked_cp_queue = pick_up_cp_queue();
			if (picked_cp_queue == -1)
				continue;

			cp_queue = &multi_user_recv_queue[picked_cp_queue].queue;

			// kthread_use_mm(multi_user_recv_queue[picked_cp_queue].mm);

			thread_read_index = cp_queue->thread_read_index;
			consume_two_entry = false;

			if (cp_queue->entries[thread_read_index].status == STATUS_WAITING) {
				if (cp_queue->entries[thread_read_index].type == TYPE_RECV_SOCKET_DATA_TWO_BLOCK) {
					consume_two_entry = true;
					next_entry_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;
					copyer_copy_out(cp_queue, &cp_queue->entries[thread_read_index], &cp_queue->entries[next_entry_index]);
					cp_queue->entries[thread_read_index].status = STATUS_DONE;
					cp_queue->entries[next_entry_index].status = STATUS_DONE;
					// printk("finish index %d and %d\n", thread_read_index, next_entry_index);
				} else {
					copyer_copy_out(cp_queue, &cp_queue->entries[thread_read_index], NULL);
					cp_queue->entries[thread_read_index].status = STATUS_DONE;
					// printk("finish index %d\n", thread_read_index);
				}
			} else {
				// printk("index %d is done\n", thread_read_index);
			}

			if (consume_two_entry)
				cp_queue->thread_read_index = (next_entry_index + 1) % DEFUALT_CP_ENTRY_NUM;
			else
				cp_queue->thread_read_index = (thread_read_index + 1) % DEFUALT_CP_ENTRY_NUM;

			// kthread_unuse_mm(multi_user_recv_queue[picked_cp_queue].mm);
		}

		while (1) {
			picked_sync_queue = pick_up_sync_queue();
			if (picked_sync_queue == -1)
				break;

			cp_queue = &multi_user_recv_queue[picked_sync_queue].queue;
			sync_queue = multi_user_recv_queue[picked_sync_queue].sync_queue;

			// kthread_use_mm(multi_user_recv_queue[picked_sync_queue].mm);
			process_sync_calls(cp_queue, &sync_queue->entries[sync_queue->thread_read_index]);
			sync_queue->entries[sync_queue->thread_read_index].status = STATUS_DONE;
			sync_queue->thread_read_index = (sync_queue->thread_read_index + 1) % DEFUALT_SYNC_ENTRY_NUM;
			// kthread_unuse_mm(multi_user_recv_queue[picked_sync_queue].mm);
		}
	}
	return 0;
}
