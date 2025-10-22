#include <copyer/copier.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>

extern int copyout_copier(void __user *to, const void *from, size_t n);
extern void *__memcpy_avx_unaligned(void *dest, const void *src, size_t n);

inline void add_start_barrier(struct cp_queue_kernel *q, struct cp_queue *uq)
{
	unsigned int write_index, uq_index = uq->write_index;
	struct cp_task *task;

	// if(uq_index){
	// 	uq_index--;
	// } else {
	// 	uq_index = QUEUE_LEN - 1;
	// }
	spin_lock(&q->lock);
	write_index = q->write_index;
	task = &q->tasks[write_index];
	task->index = uq_index;
	task->recycle_count = uq->recycle_count;
	task->type = BARRIER_START;
	q->write_index = (write_index + 1) % QUEUE_LEN;
	spin_unlock(&q->lock);
}

inline void add_end_barrier(struct cp_queue_kernel *q)
{
	unsigned int write_index;

	spin_lock(&q->lock);
	write_index = q->write_index;
	q->tasks[write_index].type = BARRIER_END;
	q->write_index = (write_index + 1) % QUEUE_LEN;
	spin_unlock(&q->lock);
}

static inline void copier_kernel_copy_to_user(struct cp_task *task)
{
	// printk("copier kernel copy to user, to %lu, len %lu\n", (u64)task->to_va, (u64)task->length);
	u8 *vaddr = kmap(task->from_page);
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = task->length;
	volatile u8 *descriptor_byte;

	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		remainingLength -= copyLength;
		if (task->descriptor) {
			offsetToWrite = (unsigned long)task->to_va - (unsigned long)task->to_va_base + offsetNow;
			descriptor_byte = (u8 *)task->descriptor + offsetToWrite / COPYLENGTH;
			if (*descriptor_byte) {
				offsetNow += copyLength;
				continue;
			}
			__memcpy_avx_unaligned(task->to_va + offsetNow, vaddr + task->from_offset + offsetNow, copyLength);
			*descriptor_byte = 1;
		} else {
			__memcpy_avx_unaligned(task->to_va + offsetNow, vaddr + task->from_offset + offsetNow, copyLength);
		}
		offsetNow += copyLength;
	}

	kunmap(task->from_page);

	if (task->skb) {
		__kfree_skb((struct sk_buff *)task->skb);
		task->skb = NULL;
	}
}

static inline void copier_kernel_copy_to_user_sync(struct cp_task *task, void *to_va, int len)
{
	u8 *vaddr = kmap(task->from_page);
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = len;
	volatile u8 *descriptor_byte;
	long from_offset = to_va - task->to_va + task->from_offset;

	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		remainingLength -= copyLength;
		if (task->descriptor) {
			offsetToWrite = (unsigned long)to_va - (unsigned long)task->to_va_base + offsetNow;
			descriptor_byte = (u8 *)task->descriptor + offsetToWrite / COPYLENGTH;
			if (*descriptor_byte) {
				offsetNow += copyLength;
				continue;
			}
			__memcpy_avx_unaligned(to_va + offsetNow, vaddr + from_offset + offsetNow, copyLength);
			*descriptor_byte = 1;
		} else {
			__memcpy_avx_unaligned(to_va + offsetNow, vaddr + from_offset + offsetNow, copyLength);
		}
		offsetNow += copyLength;
	}

	kunmap(task->from_page);

	if (task->skb) {
		__kfree_skb((struct sk_buff *)task->skb);
		task->skb = NULL;
	}
}

static inline void copier_user_copy_to_kernel(struct cp_task *task)
{
	// printk("copier user copy to kernel, from %lu, len %lu\n", (u64)task->from_va, (u64)task->length);
	__memcpy_avx_unaligned(task->to_va, task->from_va, task->length);
	*(task->descriptor_byte) = 1;
}

static inline void copier_user_copy_to_user(struct cp_task *task)
{
	// printk("copier user copy to user, from %lu, to %lu, len %lu\n", (u64)task->from_va, (u64)task->to_va, (u64)task->length);
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = task->length;
	volatile u8 *descriptor_byte;

	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		remainingLength -= copyLength;
		if (task->descriptor) {
			offsetToWrite = (unsigned long)task->to_va - (unsigned long)task->to_va_base + offsetNow;
			descriptor_byte = (u8 *)task->descriptor + offsetToWrite / COPYLENGTH;
			if (*descriptor_byte) {
				offsetNow += copyLength;
				continue;
			}
			__memcpy_avx_unaligned(task->to_va + offsetNow, task->from_va + task->from_offset + offsetNow, copyLength);
			*descriptor_byte = 1;
		} else {
			__memcpy_avx_unaligned(task->to_va + offsetNow, task->from_va + task->from_offset + offsetNow, copyLength);
		}
		offsetNow += copyLength;
	}
}

static inline void copier_user_copy_to_user_sync(struct cp_task *task, void *to_va, int len)
{
	int64_t offsetToWrite;
	int offsetNow = 0, copyLength, remainingLength = len;
	volatile u8 *descriptor_byte;
	long from_offset = to_va - task->to_va + task->from_offset;

	while (remainingLength) {
		copyLength = remainingLength > COPYLENGTH ? COPYLENGTH : remainingLength;
		remainingLength -= copyLength;
		if (task->descriptor) {
			offsetToWrite = (unsigned long)to_va - (unsigned long)task->to_va_base + offsetNow;
			descriptor_byte = (u8 *)task->descriptor + offsetToWrite / COPYLENGTH;
			if (*descriptor_byte) {
				offsetNow += copyLength;
				continue;
			}
			__memcpy_avx_unaligned(to_va + offsetNow, task->from_va + from_offset + offsetNow, copyLength);
			*descriptor_byte = 1;
		} else {
			__memcpy_avx_unaligned(to_va + offsetNow, task->from_va + from_offset + offsetNow, copyLength);
		}
		offsetNow += copyLength;
	}
}

static void process_cp_task(struct cp_task *task)
{
	if (task->status == STATUS_DONE)
		return;
	switch (task->type) {
	case TYPE_RECV_SOCKET_DATA:
		copier_kernel_copy_to_user(task);
		break;
	case TYPE_SEND_DATA:
		copier_user_copy_to_kernel(task);
		break;
	case TYPE_SIMPLE_COPY:
		copier_user_copy_to_user(task);
		break;
	default:
		printk("[copier error] unknown copy task type\n");
	}
	task->status = STATUS_DONE;
};

#define ALIGN_UP(x, a) __ALIGN_KERNEL((x) + ((a)-1), (a))

/* Simplified implementation: 
    - no recursive dependency tracking
    - traverse the entire queue
   Not used in evaluated application
*/
static void process_sync_task(struct sync_task *sync_task, struct cp_queue *cp_queue, bool is_kernel_queue)
{
	// printk("process sync task\n");
	const void *sync_start_addr = sync_task->start_addr;
	const void *sync_end_addr = sync_task->start_addr + sync_task->size;
	const int thread_read_index = cp_queue->thread_read_index;
	const int write_index = cp_queue->write_index;

	struct cp_task *cp_task;
	void *base;
	int i;
	void *sync_cp_start_addr, *sync_cp_end_addr, *cp_task_end, *cp_task_start;
	long cp_size;

	if (write_index == 0) {
		i = QUEUE_LEN - 1;
	} else {
		i = write_index - 1;
	}

	while (1) {
		cp_task = &cp_queue->tasks[i];
		if (cp_task->status == STATUS_WAITING) {
			cp_task_end = cp_task->to_va + cp_task->length;
			cp_task_start = cp_task->to_va;
			base = cp_task->to_va_base;

			if (cp_task_end <= sync_start_addr || sync_end_addr <= cp_task_start) {
				goto skip;
			}

			sync_cp_start_addr = ALIGN_DOWN(sync_start_addr - base, COPYLENGTH) + base;
			if (sync_cp_start_addr < cp_task_start) {
				sync_cp_start_addr = cp_task_start;
			}
			sync_cp_end_addr = ALIGN_UP(sync_start_addr - base, COPYLENGTH) + base;
			if (sync_cp_end_addr > cp_task_end) {
				sync_cp_end_addr = cp_task_end;
			}
			cp_size = sync_cp_end_addr - sync_cp_start_addr;

			if (is_kernel_queue) {
				copier_kernel_copy_to_user_sync(cp_task, sync_cp_start_addr, cp_size);
			} else {
				copier_user_copy_to_user_sync(cp_task, sync_cp_start_addr, cp_size);
			}
		}
	skip:
		if (i == thread_read_index)
			break;
		if (i == 0) {
			i = QUEUE_LEN - 1;
		} else {
			i--;
		}
	}
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

static inline int copier_thread_loop(void *ctx_void)
{
	struct copier_ctx *ctx = (struct copier_ctx *)ctx_void;
	struct sync_queue *sync_q = &ctx->queues->sync_queue;
	struct cp_queue *user_cp_q = &ctx->queues->user_cp_queue;
	struct cp_queue_kernel *kernel_cp_q = &ctx->queues->kernel_cp_queue;
	struct cp_task *task;
	unsigned int barrier_index = -1, recycle_count;
	bool kernel_queue_blocked = false;
	bool user_queue_blocked = false;

	kthread_use_mm(ctx->mm);

	while (!ctx->should_stop) {
		while (QUEUE_NOT_EMPTY(sync_q)) {
			process_sync_task(TAIL_TASK(sync_q), (struct cp_queue *)kernel_cp_q, 1);
			process_sync_task(TAIL_TASK(sync_q), user_cp_q, 0);
			CONSUME_TAIL_TASK(sync_q);
		}

		while (!kernel_queue_blocked && QUEUE_NOT_EMPTY(kernel_cp_q)) {
			task = TAIL_TASK(kernel_cp_q);
			if (task->type == BARRIER_START) {
				barrier_index = task->index;
				recycle_count = task->recycle_count;
				if (user_cp_q->recycle_count < recycle_count ||
				    (user_cp_q->recycle_count == recycle_count && barrier_index > user_cp_q->thread_read_index)) {
					kernel_queue_blocked = true;
				} else {
					user_queue_blocked = true;
				}
			} else if (task->type == BARRIER_END) {
				user_queue_blocked = false;
			} else {
				process_cp_task(task);
			}
			CONSUME_TAIL_TASK(kernel_cp_q);
		}

		while (!user_queue_blocked && QUEUE_NOT_EMPTY(user_cp_q)) {
			process_cp_task(TAIL_TASK(user_cp_q));
			CONSUME_TAIL_TASK_USER_QUEUE(user_cp_q);
			if (kernel_queue_blocked && user_cp_q->thread_read_index == barrier_index) {
				kernel_queue_blocked = false;
				user_queue_blocked = true;
			}
		}
	}

	kthread_unuse_mm(ctx->mm);
	mmdrop(ctx->mm);
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

static inline int copier_prep_cp_thread(struct copier_ctx *ctx)
{
	struct file *file;
	int fd;
	ctx->queues = page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
	copier_init_cp_queue_kernel(&ctx->queues->kernel_cp_queue);
	copier_init_cp_queue(&ctx->queues->user_cp_queue);
	copier_init_sync_queue(&ctx->queues->sync_queue);

	file = anon_inode_getfile("[copier]", &copier_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

static inline void copier_bind(int userfd, int threadfd)
{
	struct file *f = NULL;
	if (userfd > 0) {
		f = fget(userfd);
		if (f)
			f->copier_fd = threadfd;
	}
}

static inline void copier_prep_destory(int fd, struct copier_ctx *ctx)
{
	if (fd > 0) {
		struct file *f = fget(fd);
		f->copier_fd = -1;
	}
	__free_pages(virt_to_page(ctx->queues), get_order(1 << 21));
}

static inline int create_copier_thread(int core)
{
	struct copier_ctx *ctx;
	int fd;
	int ret;

	ctx = kmalloc(sizeof(struct copier_ctx), GFP_KERNEL);
	fd = copier_prep_cp_thread(ctx);
	ctx->should_stop = 0;
	mmgrab(current->mm);
	ctx->mm = current->mm;
	if (core >= 0) {
		ret = -EINVAL;
		if (core >= nr_cpu_ids)
			goto err;
		if (!cpu_online(core))
			goto err;
		ctx->copier_thread = kthread_create_on_cpu(copier_thread_loop, (void *)ctx, core, "copyer-wt");
	} else {
		ctx->copier_thread = kthread_create(copier_thread_loop, (void *)ctx, "copyer-wt");
	}
	if (IS_ERR(ctx->copier_thread)) {
		ret = PTR_ERR(ctx->copier_thread);
		goto err;
	}
	wake_up_process(ctx->copier_thread);

	return fd;
err:
	kfree(ctx);
	return ret;
}

SYSCALL_DEFINE2(create_cp_thread, int, fd, int, core)
{
	int t_fd = create_copier_thread(core);
	if (t_fd > 0) {
		copier_bind(fd, t_fd);
	}
	return t_fd;
}

SYSCALL_DEFINE2(del_cp_thread, int, fd, int, queue_fd)
{
	struct copier_ctx *ctx;
	struct file *q_file = fget(queue_fd);

	if (!q_file) {
		return -EFAULT;
	}
	ctx = (struct copier_ctx *)(q_file->private_data);
	ctx->should_stop = 1;
	kthread_stop(ctx->copier_thread);
	ctx->copier_thread = NULL;
	copier_prep_destory(fd, ctx);
	kfree(ctx);
	return 0;
}
