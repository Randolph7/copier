#include <linux/time.h>
#include <linux/syscalls.h>
#include "cp_memlog.h"

// #define LOG_QUEUE_LENGTH 1024 * 1024

// struct log_entry {
// 	u64 time;
// 	union {
// 		long io_length;
// 	};
// };

// struct log_entry log_queue[LOG_QUEUE_LENGTH];
// unsigned long queue_pos = 0;

// inline void add_copier_finish_log_to_queue(long io_length)
// {
// 	if (queue_pos < LOG_QUEUE_LENGTH) {
// 		log_queue[queue_pos].time = ktime_get_real_ns();
// 		log_queue[queue_pos].io_length = io_length;
// 		queue_pos++;
// 	}
// }

SYSCALL_DEFINE0(clear_memlog)
{
	// queue_pos = 0;
	return 0;
};

SYSCALL_DEFINE0(show_memlog)
{
	// int index;
	// printk("memlog dump start\n");
	// for (index = 0; index < queue_pos; index++)
	// 	printk("[ %ld ] copier io_length = %ld\n", log_queue[index].time, log_queue[index].io_length);
	// printk("memlog dump finish\n");
	return 0;
};
