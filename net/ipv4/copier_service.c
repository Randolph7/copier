#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <copyer/copier_service.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/hashtable.h>

extern int copyout_copier(void __user *to, const void *from, size_t n);
extern void *__memcpy_avx_unaligned(void *dest, const void *src, size_t n);

struct copier_ctx_sys_service service_ctx;

// static inline struct page **addr_trans(struct mm_struct *mm, const void *uva, const u32 len, int *page_num)
// {
// 	const u64 end_addr = (u64)uva + len - 1;
// 	const unsigned long nr_pages = (end_addr / PAGE_SIZE) - ((u64)uva / PAGE_SIZE) + 1;
// 	struct page **pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);

// 	mmap_read_lock(mm);
// 	if (get_user_pages_remote(mm, (u64)uva, nr_pages, FOLL_WRITE | FOLL_FORCE | FOLL_GET, pages, NULL, NULL) != nr_pages) {
// 		printk("copier addr_trans get pages error\n");
// 	}
// 	mmap_read_unlock(mm);

// 	*page_num = nr_pages;
// 	return pages;
// }

static inline void *addr_trans_cached(struct mm_struct *mm, struct at_cache *at_cache, void *uva, u32 len)
{
	const u64 end_addr = (u64)uva + len - 1;
	const u64 nr_pages = (end_addr / PAGE_SIZE) - ((u64)uva / PAGE_SIZE) + 1;
	const u64 base_offset = (u64)uva % PAGE_SIZE;
	struct page **pages;

	struct at_cache_node *cache_node;
	void *kva;

	hash_for_each_possible (at_cache->ht, cache_node, node, (u64)uva) {
		if (cache_node->uva == (u64)uva) {
			if (cache_node->page_num < nr_pages) {
				kfree(cache_node->pages);
				vfree(cache_node->kva);
				pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);

				mmap_read_lock(mm);
				if (get_user_pages_remote(mm, (u64)uva, nr_pages, FOLL_WRITE | FOLL_FORCE | FOLL_GET, pages, NULL, NULL) !=
				    nr_pages) {
					printk("copier addr_trans get pages error\n");
				}
				mmap_read_unlock(mm);

				cache_node->pages = pages;
				cache_node->page_num = nr_pages;
				kva = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
				cache_node->kva = kva;
				// printk("kva from %lu, page_num = %lu\n", (u64)kva, nr_pages);
				return kva + base_offset;
			} else {
				// printk("cache hit, return %lu\n", cache_node->kva + base_offset);
				return cache_node->kva + base_offset;
			}
		}
	}
	if (at_cache->pool_index == COPIER_HASH_NODE_POLL_SIZE) {
		// cache eviction not implemented
		printk("cache pool full\n");
		return NULL;
	}
	cache_node = &at_cache->nodes_pool[at_cache->pool_index];
	at_cache->pool_index++;

	pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);

	mmap_read_lock(mm);
	if (get_user_pages_remote(mm, (u64)uva, nr_pages, FOLL_WRITE | FOLL_FORCE | FOLL_GET, pages, NULL, NULL) != nr_pages) {
		printk("copier addr_trans get pages error\n");
	}
	mmap_read_unlock(mm);

	cache_node->uva = (u64)uva;
	cache_node->pages = pages;
	cache_node->page_num = nr_pages;
	kva = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	cache_node->kva = kva;

	// printk("kva from %lu, phy = %lu, page_num = %lu\n", (u64)kva, page_to_phys(pages[0]), nr_pages);

	hash_add(at_cache->ht, &cache_node->node, (u64)uva);

	return kva + base_offset;
}

static inline u32 copier_kernel_copy_to_user(struct cp_task *task, struct mm_struct *mm, struct at_cache *at_cache)
{
	// printk("copier kernel copy to user, to %lu, len %lu\n", (u64)task->to_va, (u64)task->length);
	const u8 *from_kva = kmap(task->from_page);
	const int64_t to_va_offset = (unsigned long)task->to_va - (unsigned long)task->to_va_base;
	// int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = task->length;
	// volatile u8 *descriptor_byte;
	volatile int64_t *descriptor = NULL;

	void *to_kva;
	// void *descriptor_kva = NULL;

	to_kva = addr_trans_cached(mm, at_cache, task->to_va_base, remainingLength + to_va_offset) + to_va_offset;

	if (task->descriptor) {
		// TODO: remove hard-coded 1024
		descriptor = addr_trans_cached(mm, at_cache, task->descriptor, 8);
	}

	if (descriptor) {
		while (remainingLength) {
			copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
			remainingLength -= copyLength;
			// offsetToWrite = to_va_offset + offsetNow;
			// descriptor_byte = (u8 *)descriptor_kva + offsetToWrite / COPYLENGTH;
			__memcpy_avx_unaligned(to_kva + offsetNow, from_kva + task->from_offset + offsetNow, copyLength);
			offsetNow += copyLength;
			*descriptor = to_va_offset + offsetNow - 1;
		}
	} else {
		__memcpy_avx_unaligned(to_kva, from_kva + task->from_offset, remainingLength);
	}

	kunmap(task->from_page);

	return task->length;
}

static inline u32 copier_user_copy_to_kernel(struct cp_task *task, struct mm_struct *mm, struct at_cache *at_cache)
{
	// printk("copier user copy to kernel, from %lu, len %lu\n", (u64)task->from_va, (u64)task->length);
	void *from_kva = addr_trans_cached(mm, at_cache, task->from_va, task->length);
	__memcpy_avx_unaligned(task->to_va, from_kva, task->length);
	*(task->descriptor_byte) = 1;
	return task->length;
}

static inline u32 copier_user_copy_to_user(struct cp_task *task, struct mm_struct *mm, struct at_cache *at_cache)
{
	// printk("copier user copy to user, from %lu, to %lu, len %lu\n", (u64)task->from_va, (u64)task->to_va, (u64)task->length);
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = task->length;
	volatile u8 *descriptor_byte;
	const u64 to_va = (u64)task->to_va;
	const u64 to_va_offset = to_va - (u64)task->to_va_base;
	const u64 from_va_offset = task->from_va - task->from_va_base;

	void *from_kva;
	void *descriptor_kva = NULL;
	int to_pages_num = -1;
	u32 cur_to_page_index = 0, cur_to_page_offset = to_va % PAGE_SIZE, cur_to_page_offset_tmp;
	void *cur_to_page_kva;
	struct page *to_pages[10];

	const unsigned long nr_to_pages = ((to_va + remainingLength) / PAGE_SIZE) - (to_va / PAGE_SIZE) + 1;

	if (nr_to_pages > 10) {
		printk("error too many pages\n");
		return task->length;
	}

	from_kva = addr_trans_cached(mm, at_cache, task->from_va_base, remainingLength + from_va_offset) + from_va_offset;

	if (task->descriptor) {
		descriptor_kva = addr_trans_cached(mm, at_cache, task->descriptor, 1024);
	}

	mmap_read_lock(mm);
	to_pages_num = get_user_pages_remote(mm, to_va, nr_to_pages, FOLL_WRITE | FOLL_FORCE | FOLL_GET, to_pages, NULL, NULL);
	mmap_read_unlock(mm);

	// to_pages = addr_trans(mm, task->to_va, remainingLength, &to_pages_num);
	if (to_pages_num != nr_to_pages) {
		printk("copier_user_copy_to_user get pages error\n");
		return task->length;
	}

	cur_to_page_kva = page_to_virt(to_pages[0]);
	if (descriptor_kva) {
		while (remainingLength) {
			copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
			remainingLength -= copyLength;
			offsetToWrite = to_va_offset + offsetNow;
			descriptor_byte = (u8 *)descriptor_kva + offsetToWrite / COPYLENGTH;
			cur_to_page_offset_tmp = cur_to_page_offset + copyLength;
			if (cur_to_page_offset_tmp < PAGE_SIZE) {
				__memcpy_avx_unaligned(cur_to_page_kva + cur_to_page_offset, from_kva + offsetNow, copyLength);
				cur_to_page_offset = cur_to_page_offset_tmp;
			} else {
				__memcpy_avx_unaligned(cur_to_page_kva + cur_to_page_offset, from_kva + offsetNow, PAGE_SIZE - cur_to_page_offset);
				cur_to_page_index++;
				cur_to_page_kva = page_to_virt(to_pages[cur_to_page_index]);
				cur_to_page_offset = cur_to_page_offset_tmp - PAGE_SIZE;
				__memcpy_avx_unaligned(cur_to_page_kva, from_kva + offsetNow, cur_to_page_offset);
			}

			*descriptor_byte = 1;
			offsetNow += copyLength;
		}

	} else {
		for (; cur_to_page_index < to_pages_num;
		     cur_to_page_index++, cur_to_page_kva = page_to_virt(to_pages[cur_to_page_index]), cur_to_page_offset = 0) {
			copyLength = remainingLength > (PAGE_SIZE - cur_to_page_offset) ? (PAGE_SIZE - cur_to_page_offset) : remainingLength;
			remainingLength -= copyLength;
			__memcpy_avx_unaligned(cur_to_page_kva + cur_to_page_offset, from_kva + offsetNow, copyLength);
			// printk("from %lu to %lu\n", (u64)cur_to_page_kva + cur_to_page_offset, (u64)cur_to_page_kva + cur_to_page_offset + copyLength - 1);
			offsetNow += copyLength;
		}
	}
	return task->length;
}

static inline void skb_release(struct sk_buff *skb)
{
	__kfree_skb(skb);
}

static inline u32 process_cp_task(struct cp_task *task, struct mm_struct *mm, struct at_cache *at_cache)
{
	u32 ret;
	if (task->status == STATUS_DONE)
		return 0;
	switch (task->type) {
	case TYPE_RECV_SOCKET_DATA:
		ret = copier_kernel_copy_to_user(task, mm, at_cache);
		break;
	case TYPE_SEND_DATA:
		ret = copier_user_copy_to_kernel(task, mm, at_cache);
		break;
	case TYPE_SIMPLE_COPY:
		ret = copier_user_copy_to_user(task, mm, at_cache);
		break;
	case TYPE_SOCKET_RELEASE_SKB:
		ret = 0;
		skb_release(task->skb);
		break;
	default:
		ret = 0;
		printk("[copier error] unknown copy task type\n");
	}
	task->status = STATUS_DONE;
	return ret;
};

static inline struct copier_scheduler_node *get_next_process(void)
{
	struct rb_node *node = rb_first(&service_ctx.index_root);
	if (!node) {
		return NULL;
	}
	return container_of(node, struct copier_scheduler_node, node);
}

static inline void add_scheduler_index_node(struct copier_scheduler_node *index_node, u32 key)
{
	struct copier_scheduler_node *this;
	struct rb_node **new;
	struct rb_node *parent = NULL;
	new = &(service_ctx.index_root.rb_node);

	while (*new) {
		this = container_of(*new, struct copier_scheduler_node, node);
		parent = *new;
		if (key < this->cp_length)
			new = &((*new)->rb_left);
		else if (key > this->cp_length)
			new = &((*new)->rb_right);
		else {
			printk("copier rb_tree key conflict\n");
			return;
		}
	}

	rb_link_node(&index_node->node, parent, new);
	rb_insert_color(&index_node->node, &service_ctx.index_root);
}

static inline void update_scheduler_index(struct copier_scheduler_node *index_node, u32 cp_length)
{
	rb_erase(&index_node->node, &service_ctx.index_root);
	index_node->cp_length = cp_length;
	add_scheduler_index_node(index_node, cp_length);
}

#define QUEUE_NOT_EMPTY(queue) (queue->thread_read_index != queue->write_index)
#define TAIL_TASK(queue) (&queue->tasks[queue->thread_read_index])
#define CONSUME_TAIL_TASK_USER_QUEUE(queue)                                                                                                          \
	{                                                                                                                                            \
		queue->thread_read_index = (queue->thread_read_index + 1) % QUEUE_LEN;                                                               \
		if (queue->thread_read_index == 0) {                                                                                                 \
			queue->recycle_count++;                                                                                                      \
		}                                                                                                                                    \
	}

#define CONSUME_TAIL_TASK(queue)                                                                                                                     \
	{                                                                                                                                            \
		queue->thread_read_index = (queue->thread_read_index + 1) % QUEUE_LEN;                                                               \
	}

int last_scheduled = -1;
static inline int copier_thread_loop(void *useless)
{
	struct cp_queue **user_cp_qs = service_ctx.user_cp_queue;
	struct cp_queue_kernel **kernel_cp_qs = service_ctx.kernel_cp_queue;
	bool *kernel_queue_blocked = service_ctx.kernel_queue_blocked;
	bool *user_queue_blocked = service_ctx.user_queue_blocked;
	u32 *barrier_indexs = service_ctx.barrier_index;
	struct mm_struct **mms = service_ctx.mm;
	struct at_cache *atcaches = service_ctx.at_cache;
	spinlock_t *scheduler_lock = &service_ctx.scheduler_index_lock;

	u32 recycle_count;
	u32 barrier_index;
	struct cp_task *task;
	struct copier_scheduler_node *process_served = NULL;
	u32 copy_length;
	u16 process_index;
	bool once_more;
	struct cp_queue *user_cp_q;
	struct cp_queue_kernel *kernel_cp_q;
	struct mm_struct *mm;
	struct at_cache *atcache;
	u32 copy_length_added;
	// int loop_count = 0;

	UNUSED(useless);

	while (!service_ctx.should_stop) {
		spin_lock(scheduler_lock);
		process_served = get_next_process();
		spin_unlock(scheduler_lock);
		if (!process_served) {
			continue;
		}
		copy_length = process_served->cp_length;
		process_index = process_served->queue_index;
		printk("pick up index%u\n", process_index);
		if (service_ctx.queue_count == 0)
			continue;
		
		/* RR scheduler*/
		process_index = (last_scheduled + 1) % service_ctx.queue_count;
		// printk("index = %d\n", process_index);
		last_scheduled = process_index;
		/* end RR scheduler*/

		user_cp_q = user_cp_qs[process_index];
		kernel_cp_q = kernel_cp_qs[process_index];
		mm = mms[process_index];
		atcache = &atcaches[process_index];
		copy_length_added = 0;

	serve_client:
		once_more = false;

		// while (QUEUE_NOT_EMPTY(sync_q)) {
		// 	process_sync_task(TAIL_TASK(sync_q), (struct cp_queue *)kernel_cp_q, 1);
		// 	process_sync_task(TAIL_TASK(sync_q), user_cp_q, 0);
		// 	CONSUME_TAIL_TASK(sync_q);
		// }

		if (!kernel_queue_blocked[process_index] && QUEUE_NOT_EMPTY(kernel_cp_q)) {
			task = TAIL_TASK(kernel_cp_q);
			if (task->type == BARRIER_START) {
				once_more = true;
				barrier_index = task->index;
				recycle_count = task->recycle_count;
				if (user_cp_q->recycle_count < recycle_count ||
				    (user_cp_q->recycle_count == recycle_count && barrier_index > user_cp_q->thread_read_index)) {
					barrier_indexs[process_index] = task->index;
					kernel_queue_blocked[process_index] = true;
				} else {
					user_queue_blocked[process_index] = true;
				}
			} else if (task->type == BARRIER_END) {
				once_more = true;
				user_queue_blocked[process_index] = false;
			} else {
				copy_length_added = process_cp_task(task, mm, atcache);
			}
			CONSUME_TAIL_TASK(kernel_cp_q);
		}

		else if (!user_queue_blocked[process_index] && QUEUE_NOT_EMPTY(user_cp_q)) {
			copy_length_added = process_cp_task(TAIL_TASK(user_cp_q), mm, atcache);
			CONSUME_TAIL_TASK_USER_QUEUE(user_cp_q);
			if (kernel_queue_blocked[process_index] && user_cp_q->thread_read_index == barrier_indexs[process_index]) {
				kernel_queue_blocked[process_index] = false;
				user_queue_blocked[process_index] = true;
			}
		}

		if (once_more)
			goto serve_client;

		if (copy_length_added) {
			copy_length += copy_length_added & (!MAX_COPIER_CLIENT_NUM);
			update_scheduler_index(process_served, copy_length);
		}
	}

	return 0;
}

static int copier_queue_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int copier_queue_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct copier_ctx *ctx = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(ctx->queues) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

const static struct file_operations copier_fops = {
	.mmap = copier_queue_mmap,
	.release = copier_queue_release,
};

static inline void copier_init_cp_queue(struct cp_queue *queue)
{
	int i;
	memset(queue, 0, sizeof(struct cp_queue));
	for (i = 0; i < QUEUE_LEN; i++) {
		queue->tasks[i].status = STATUS_DONE;
	}
}

static inline void copier_init_cp_queue_kernel(struct cp_queue_kernel *queue)
{
	int i;
	memset(queue, 0, sizeof(struct cp_queue));
	for (i = 0; i < QUEUE_LEN; i++) {
		queue->tasks[i].status = STATUS_DONE;
	}
	spin_lock_init(&queue->lock);
}

static inline void copier_init_sync_queue(struct sync_queue *queue)
{
	int i;
	memset(queue, 0, sizeof(struct sync_queue));
	for (i = 0; i < QUEUE_LEN; i++) {
		queue->tasks[i].status = STATUS_DONE;
	}
}

SYSCALL_DEFINE0(create_cp_queue)
{
	struct copier_ctx *queue_ctx;
	struct process_queues *queues;
	struct file *file;
	int fd;
	u16 queue_count;
	struct copier_scheduler_node *index_node;
	int i;
	struct at_cache *at_cache;

	queue_ctx = kmalloc(sizeof(struct copier_ctx), GFP_KERNEL);
	queues = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	queue_ctx->queues = queues;
	copier_init_cp_queue_kernel(&queues->kernel_cp_queue);
	copier_init_cp_queue(&queues->user_cp_queue);
	copier_init_sync_queue(&queues->sync_queue);

	file = anon_inode_getfile("[copier]", &copier_fops, queue_ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);

	queue_count = service_ctx.queue_count;
	service_ctx.kernel_cp_queue[queue_count] = &queues->kernel_cp_queue;
	service_ctx.user_cp_queue[queue_count] = &queues->user_cp_queue;
	service_ctx.kernel_queue_blocked[queue_count] = false;
	service_ctx.user_queue_blocked[queue_count] = false;
	service_ctx.barrier_index[queue_count] = -1;
	service_ctx.queue_ctx[queue_count] = queue_ctx;

	mmgrab(current->mm);
	service_ctx.mm[queue_count] = current->mm;
	// printk("mm = %lu\n", (uint64_t)service_ctx.mm[queue_count]);

	index_node = &service_ctx.scheduler_node[queue_count];
	RB_CLEAR_NODE(&index_node->node);
	index_node->cp_length = queue_count;
	index_node->queue_index = queue_count;
	spin_lock(&service_ctx.scheduler_index_lock);
	add_scheduler_index_node(index_node, queue_count);
	spin_unlock(&service_ctx.scheduler_index_lock);

	at_cache = &service_ctx.at_cache[queue_count];
	memset(at_cache, 0, sizeof(struct at_cache));
	for (i = 0; i < 16; i++) {
		INIT_HLIST_HEAD(&at_cache->ht[i]);
	}

	service_ctx.queue_count++;
	return fd;
}

SYSCALL_DEFINE1(del_cp_queue, int, queue_fd)
{
	struct copier_ctx *queue_ctx;
	int queue_count;

	struct file *q_file = fget(queue_fd);
	if (!q_file) {
		return -EFAULT;
	}
	queue_ctx = (struct copier_ctx *)(q_file->private_data);

	__free_pages(virt_to_page(queue_ctx->queues), get_order(1 << 21));

	for (queue_count = 0; queue_count < service_ctx.queue_count; queue_count++) {
		if (service_ctx.queue_ctx[queue_count] == queue_ctx) {
			break;
		}
	}

	service_ctx.queue_count--;

	// spin_lock(&service_ctx.scheduler_index_lock);
	// rb_erase(&service_ctx.scheduler_node[queue_count].node, &service_ctx.index_root);
	// spin_unlock(&service_ctx.scheduler_index_lock);

	kfree(service_ctx.queue_ctx[queue_count]);
	service_ctx.queue_ctx[queue_count] = NULL;

	// queue_count = service_ctx.queue_count - 1;
	// while (!service_ctx.queue_ctx[queue_count]) {
	// 	queue_count--;
	// }
	// queue_count = service_ctx.queue_count + 1;
	// service_ctx.queue_count = queue_count;

	return 0;
}

static inline int create_copier_service(int core)
{
	int ret = 0;

	memset(&service_ctx, 0, sizeof(struct copier_ctx_sys_service));
	service_ctx.index_root = RB_ROOT;
	spin_lock_init(&service_ctx.scheduler_index_lock);
	if (core >= 0) {
		ret = -EINVAL;
		if (core >= nr_cpu_ids)
			goto err;
		if (!cpu_online(core))
			goto err;
		service_ctx.copier_thread = kthread_create_on_cpu(copier_thread_loop, NULL, core, "copyer-service-wt");
	} else {
		service_ctx.copier_thread = kthread_create(copier_thread_loop, NULL, "copyer-service-wt");
	}
	if (IS_ERR(service_ctx.copier_thread)) {
		ret = PTR_ERR(service_ctx.copier_thread);
		goto err;
	}
	wake_up_process(service_ctx.copier_thread);

	return 0;
err:
	return ret;
}

SYSCALL_DEFINE1(create_copier_service, int, core)
{
	return create_copier_service(core);
}

SYSCALL_DEFINE0(del_copier_service)
{
	service_ctx.should_stop = 1;
	kthread_stop(service_ctx.copier_thread);
	service_ctx.copier_thread = NULL;
	return 0;
}
