#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

#define MAX_QUEUE_NUM (10)

struct multi_user_recv_queue_struct multi_user_recv_queue[MAX_QUEUE_NUM];
struct cp_queue *recv_queue_address_cache[MAX_QUEUE_NUM];
volatile int recv_queue_last_allocated = -1;
DEFINE_SPINLOCK(recv_queue_alloc_lock);
struct task_struct *whole_system_recv_cp_thread = NULL;
struct cp_thread_whole_system_ctx recv_ctx;

struct multi_user_send_queue_struct multi_user_send_queue[MAX_QUEUE_NUM];
struct cp_in_queue *send_queue_address_cache[MAX_QUEUE_NUM];
volatile int send_queue_last_allocated = -1;
DEFINE_SPINLOCK(send_queue_alloc_lock);
struct task_struct *whole_system_send_cp_thread = NULL;
struct cp_thread_whole_system_ctx send_ctx;

extern const struct file_operations queue_fops;
extern const struct file_operations empty_fops;
extern void init_cp_queue(struct cp_queue *queue);
extern void init_sync_queue(struct sync_queue *queue);
extern void init_cp_in_queue(struct cp_in_queue *queue);

SYSCALL_DEFINE2(create_cp_thread_whole_system, __u64, core, int, type)
{
	int ret = -1, i;

	if (type == QUEUE_TYPE_OUT) {
		recv_ctx.should_stop = 0;
		if (core >= 0) {
			ret = -EINVAL;
			if (core >= nr_cpu_ids)
				goto err;
			if (!cpu_online(core))
				goto err;
			whole_system_recv_cp_thread =
				kthread_create_on_cpu(thread_background_cp_whole_system, (void *)&recv_ctx, core, "whole_sys_copyer-wt");
		} else {
			whole_system_recv_cp_thread = kthread_create(thread_background_cp_whole_system, (void *)&recv_ctx, "whole_sys_copyer-wt");
		}
		if (IS_ERR(whole_system_recv_cp_thread)) {
			ret = PTR_ERR(whole_system_recv_cp_thread);
			goto err;
		}

		recv_queue_last_allocated = -1;
		for (i = 0; i < MAX_QUEUE_NUM; i++) {
			multi_user_recv_queue[i].queues_for_recv =
				page_address(alloc_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO, get_order(1 << 21)));
			init_cp_queue(&multi_user_recv_queue[i].queues_for_recv->queue);
			// init_sync_queue(&multi_user_recv_queue[i].queues_for_recv->sync_queue);
			multi_user_recv_queue[i].valid = 0;
			recv_queue_address_cache[i] = &multi_user_recv_queue[i].queues_for_recv->queue;
		}
		wake_up_process(whole_system_recv_cp_thread);
	} else if (type == QUEUE_TYPE_IN) {
		send_ctx.should_stop = 0;
		if (core >= 0) {
			ret = -EINVAL;
			if (core >= nr_cpu_ids)
				goto err;
			if (!cpu_online(core))
				goto err;
			whole_system_send_cp_thread =
				kthread_create_on_cpu(thread_background_cp_in_whole_system, (void *)&send_ctx, core, "whole_sys_copyer-wt-send");
		} else {
			whole_system_send_cp_thread =
				kthread_create(thread_background_cp_in_whole_system, (void *)&send_ctx, "whole_sys_copyer-wt-send");
		}
		if (IS_ERR(whole_system_send_cp_thread)) {
			ret = PTR_ERR(whole_system_send_cp_thread);
			goto err;
		}

		send_queue_last_allocated = -1;
		for (i = 0; i < MAX_QUEUE_NUM; i++) {
			init_cp_in_queue(&multi_user_send_queue[i].cp_in_queue);
			multi_user_send_queue[i].valid = 0;
			send_queue_address_cache[i] = &multi_user_send_queue[i].cp_in_queue;
		}
		wake_up_process(whole_system_send_cp_thread);
	}

err:
	return ret;
}

SYSCALL_DEFINE3(create_cp_queue_whole_system, int, user_fd, __u64, core, int, type)
{
	int i, queue_index = -1;
	struct file *f = NULL;
	struct copyer_ctx *ctx;
	struct file *file;
	int fd = -1;

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);

	if (type == QUEUE_TYPE_OUT) {
		if (!whole_system_recv_cp_thread)
			__do_sys_create_cp_thread_whole_system(core, type);
		spin_lock(&recv_queue_alloc_lock);
		for (i = 0; i <= recv_queue_last_allocated; i++)
			if (!multi_user_recv_queue[i].valid) {
				queue_index = i;
				break;
			}
		if (queue_index == -1) {
			recv_queue_last_allocated++;
			queue_index = recv_queue_last_allocated;
		}
		if (queue_index >= 0)
			multi_user_recv_queue[queue_index].valid = 1;
		spin_unlock(&recv_queue_alloc_lock);
		if (queue_index < 0) {
			printk("not available queue\n");
			return 0;
		}
		ctx->queues_for_recv = multi_user_recv_queue[queue_index].queues_for_recv;
		ctx->queue_index = queue_index;
		mmgrab(current->mm);
		multi_user_recv_queue[queue_index].mm = current->mm;

		file = anon_inode_getfile("[cp_thread_recv]", &queue_fops, ctx, O_RDWR | O_CLOEXEC);
		fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
		if (fd < 0)
			return fd;
		fd_install(fd, file);
		if (user_fd > 0) {
			f = fget(user_fd);
			if (f)
				f->queue_fd_out = fd;
		}
	}

	if (type == QUEUE_TYPE_IN) {
		if (!whole_system_send_cp_thread)
			__do_sys_create_cp_thread_whole_system(core, type);
		spin_lock(&send_queue_alloc_lock);
		for (i = 0; i <= send_queue_last_allocated; i++)
			if (!multi_user_send_queue[i].valid) {
				queue_index = i;
				break;
			}
		if (queue_index == -1) {
			send_queue_last_allocated++;
			queue_index = send_queue_last_allocated;
		}
		if (queue_index >= 0)
			multi_user_send_queue[queue_index].valid = 1;
		spin_unlock(&send_queue_alloc_lock);
		if (queue_index < 0) {
			printk("not available queue\n");
			return 0;
		}
		ctx->queue_in = &multi_user_send_queue[queue_index].cp_in_queue;
		ctx->queue_index = queue_index;

		file = anon_inode_getfile("[cp_thread_send]", &empty_fops, ctx, O_RDWR | O_CLOEXEC);
		fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
		if (fd < 0)
			return fd;
		fd_install(fd, file);
		if (user_fd > 0) {
			f = fget(user_fd);
			if (f)
				f->queue_fd_in = fd;
		}
	}

	return fd;
}

SYSCALL_DEFINE3(release_cp_queue_whole_system, int, user_fd, int, queue_fd, int, type)
{
	struct file *f = fget(queue_fd);
	struct copyer_ctx *ctx = (struct copyer_ctx *)f->private_data;

	if (type == QUEUE_TYPE_OUT) {
		spin_lock(&recv_queue_alloc_lock);
		multi_user_recv_queue[ctx->queue_index].valid = 0;
		while (recv_queue_last_allocated >= 0 && multi_user_recv_queue[recv_queue_last_allocated].valid == 0)
			recv_queue_last_allocated--;
		init_cp_queue(&ctx->queues_for_recv->queue);
		// init_sync_queue(&ctx->queues_for_recv->sync_queue);
		spin_unlock(&recv_queue_alloc_lock);
		if (user_fd > 0) {
			f = fget(user_fd);
			if (f)
				f->queue_fd_out = -1;
		}
	}
	if (type == QUEUE_TYPE_IN) {
		spin_lock(&send_queue_alloc_lock);
		multi_user_send_queue[ctx->queue_index].valid = 0;
		while (send_queue_last_allocated >= 0 && multi_user_send_queue[send_queue_last_allocated].valid == 0)
			send_queue_last_allocated--;
		init_cp_in_queue(ctx->queue_in);
		spin_unlock(&send_queue_alloc_lock);
		if (user_fd > 0) {
			f = fget(user_fd);
			if (f)
				f->queue_fd_in = -1;
		}
	}

	fput(f);

	return 0;
}

SYSCALL_DEFINE3(bind_cp_thread_whole_system, int, fd, int, queue_fd, int, type)
{
	struct file *f = NULL;
	switch (type) {
	case QUEUE_TYPE_OUT:
		if (fd > 0) {
			f = fget(fd);
			if (f)
				f->queue_fd_out = queue_fd;
		}
		break;
	case QUEUE_TYPE_IN:
		if (fd > 0) {
			f = fget(fd);
			if (f)
				f->queue_fd_in = queue_fd;
		}
		break;
	default:
		printk("unknown queue type in bind_cp_thread_whole_system!\n");
	}

	return fd;
}

SYSCALL_DEFINE1(del_cp_thread_whole_system, int, type)
{
	int i;

	if (type == QUEUE_TYPE_OUT) {
		recv_ctx.should_stop = 1;
		kthread_stop(whole_system_recv_cp_thread);
		whole_system_recv_cp_thread = NULL;
		for (i = 0; i < MAX_QUEUE_NUM; i++) {
			__free_pages(virt_to_page(multi_user_recv_queue[i].queues_for_recv), get_order(1 << 21));
			multi_user_recv_queue[i].queues_for_recv = NULL;
		}
	}
	if (type == QUEUE_TYPE_IN) {
		send_ctx.should_stop = 1;
		kthread_stop(whole_system_send_cp_thread);
		whole_system_send_cp_thread = NULL;
	}
	return 0;
}
