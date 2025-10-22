#include <crypto/hash.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/inet_diag.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/scatterlist.h>
#include <linux/splice.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/random.h>
#include <linux/memblock.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/errqueue.h>
#include <linux/static_key.h>
#include <linux/btf.h>
#include <linux/uio.h>
#include <net/icmp.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/busy_poll.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/sched/task.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

/*  

For the sake of simplicity, many issues are currently not considered:
  - repeated copying to the same address 
  - usr-buffer being released
  - priority of background copying 

*/

/*
  very coarse-grained 

  TODO: Fine-grained
*/
// static inline void mark_entry_aborted_and_sync_copy(struct copyer_ctx *ctx, void __user *to_va_base, int offset,
// 						    int length)
// {
// 	struct cp_queue *queue = ctx->queue;
// 	unsigned int usr_write_index = queue->thread_read_index;
// 	unsigned int thread_read_index = queue->thread_read_index;
// 	int end_offset;

// 	while (thread_read_index != usr_write_index) {
// 		if (queue->entries[thread_read_index].type != TYPE_SOCKET_RELEASE_SKB &&
// 		    spin_trylock(&queue->locks[thread_read_index])) {
// 			end_offset = queue->entries[thread_read_index].to_va_offset +
// 				     queue->entries[thread_read_index].length;
// 			if (queue->entries[thread_read_index].to_va_base == to_va_base && end_offset > offset &&
// 			    queue->entries[thread_read_index].status == STATUS_WAITING) {
// 				copyer_copy_out(ctx, &queue->entries[thread_read_index]);
// 				queue->entries[thread_read_index].status = STATUS_DONE;
// 			}
// 			spin_unlock(&queue->locks[thread_read_index]);
// 		}
// 		thread_read_index++;
// 	}
// }

SYSCALL_DEFINE4(sync_cp_copyer, int, fd, void __user *, base_va, int, offset, int, length)
{
	// struct file *f;
	// f = fget(fd);
	// if (!f || !f->private_data)
	// 	return -1;
	// struct copyer_ctx *qlt = (struct copyer_ctx *)f->private_data;
	// if (!qlt->queue)
	// 	return -1;
	// mark_entry_aborted_and_sync_copy(qlt, base_va, offset, length);
	return 0;
}