#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

#define step 4096
// refere to net/ipv4/cp_send_thread.c and net/ipv4/cp_recv_thread.c

static int binder_queue_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations binder_queue_fops = {
	.release = binder_queue_release,
};

static inline void init_binder_queue(struct binder_queue *queue)
{
	memset(queue, 0, sizeof(struct binder_queue));
}

static inline int thread_background_cp_binder(void *ctx_void)
{
	//TODO: variable definitions here
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	unsigned int binder_read_index, binder_write_index;
	struct binder_queue *b_queue = ctx->b_queue;
	struct page *page_first;
	void *kptr;
	void *kptr_first;
	int ret;

	int copyLength,copied_size=0;
	unsigned long value=0;
	KTHREAD_PREPARE_MM(ctx);

	while (!ctx->should_stop) {
		//TODO: main routine here
		binder_read_index = b_queue->binder_read_index;
		binder_write_index = b_queue->binder_write_index;
		if (binder_read_index != binder_write_index) {
			switch (b_queue->entries[binder_read_index].type) {
				case CP_START:
					page_first=b_queue->entries[binder_read_index].page;
					kptr_first = kmap(b_queue->entries[binder_read_index].page) + b_queue->entries[binder_read_index].pgoff;
					ret = copy_from_user(kptr_first, b_queue->entries[binder_read_index].from, b_queue->entries[binder_read_index].size);
					value+=4096;
					break;
				case CP_MID:
					copied_size=0;
					kptr = kmap(b_queue->entries[binder_read_index].page) + b_queue->entries[binder_read_index].pgoff;
					while(copied_size < b_queue->entries[binder_read_index].size){
						copyLength=(b_queue->entries[binder_read_index].size-copied_size) > step ? step:(b_queue->entries[binder_read_index].size-copied_size);
						ret = copy_from_user(kptr+copied_size, b_queue->entries[binder_read_index].from+copied_size, copyLength);
						copied_size+=copyLength;
						value+=copyLength;
						memcpy(kptr_first, &value, sizeof(unsigned long));
					}
					kunmap(b_queue->entries[binder_read_index].page);
					break;
				case CP_END:
					copied_size=0;
					kptr = kmap(b_queue->entries[binder_read_index].page) + b_queue->entries[binder_read_index].pgoff;
					while(copied_size<b_queue->entries[binder_read_index].size){
						copyLength=(b_queue->entries[binder_read_index].size-copied_size) > step ? step:(b_queue->entries[binder_read_index].size-copied_size);
						ret = copy_from_user(kptr+copied_size, b_queue->entries[binder_read_index].from+copied_size, copyLength);
						copied_size+=copyLength;
						value+=copyLength;
						memcpy(kptr_first, &value, sizeof(unsigned long));
					}
					kunmap(b_queue->entries[binder_read_index].page);
					kunmap(page_first);
					break;
				case CP_ONCE:
					kptr = kmap(b_queue->entries[binder_read_index].page) + b_queue->entries[binder_read_index].pgoff;
					ret = copy_from_user(kptr, b_queue->entries[binder_read_index].from, b_queue->entries[binder_read_index].size);
					value=4096;
					memcpy(kptr, &value, sizeof(unsigned long));
					kunmap(b_queue->entries[binder_read_index].page);
					break;
				default:
					//error
					break;
			}
			b_queue->binder_read_index = (binder_read_index + 1) % DEFUALT_CP_BINDER_ENTRY_NUM;
		}
	}
	KTHREAD_DROP_MM(ctx);
	return 0;
}

inline int binder_prep_cp_thread(struct copyer_ctx *ctx)
{
	struct file *file;
	int fd;
	ctx->b_queue = vmalloc(sizeof(struct binder_queue));

	if (!ctx->b_queue)
		printk("fail to alloc cp queue!\n");
	init_binder_queue(ctx->b_queue);
	ctx->thread_func = thread_background_cp_binder;
	file = anon_inode_getfile("[cp_thread_binder]", &binder_queue_fops, ctx, O_RDWR | O_CLOEXEC);
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

inline void binder_bind(int userfd, int threadfd)
{
	struct file *f = NULL;
	if (userfd > 0) {
		f = fget(userfd);
		if (f)
			f->binder_fd = threadfd;
	}
}

inline void binder_prep_destory(int fd, struct copyer_ctx *ctx)
{
	if (fd > 0) {
		struct file *f = fget(fd);
		f->binder_fd = -1;
	}
	vfree(ctx->b_queue);
}
