#include <linux/spinlock.h>
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
#include <linux/time.h>

extern int copyout(void __user *to, const void *from, size_t n);

SYSCALL_DEFINE4(test_cp_to_user, long, size, void __user *, to, int, prewarm, int, repeat)
{
	int i;
	long time = 0, size_cp = 0;
	void *from = kmalloc(size, GFP_KERNEL);
	u64 t1, t2;

	memset(from, 0, size);
	for (i = 0; i < prewarm; i++)
		copyout(to, from, size);
	for (i = 0; i < repeat; i++) {
		t1 = ktime_get_real_ns();
		size_cp += copyout(to, from, size);
		t2 = ktime_get_real_ns();
		time += t2 - t1;
	}

	kfree(from);
	printk("test_cpyout size = %ldK\n", size_cp / 1024 / repeat);
	return time / repeat;
}