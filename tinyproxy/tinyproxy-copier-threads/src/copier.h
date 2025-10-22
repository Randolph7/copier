#include <stdint.h>

#define SEND_CORE 2
#define QUEUE_TYPE_IN_LAZY 6

#define LazyRecv(sockfd, buf, len, flags) syscall(614, sockfd, buf, len, flags, NULL, 0)
#define LazySend(fd, buf, n, flags, lazy_fd) syscall(616, fd, buf, n, flags, lazy_fd)
#define LazyRecvInit() syscall(615)
#define CreateCpThread() syscall(602, -1, SEND_CORE, QUEUE_TYPE_IN_LAZY)
#define BindCpThread(fd, queue_fd) syscall(605, fd, queue_fd, QUEUE_TYPE_IN_LAZY)
#define DelCpThread(queue_fd) syscall(603, -1, queue_fd, QUEUE_TYPE_IN_LAZY);

#define TYPE_SIMPLE_COPY 5
#define DEFUALT_CP_IN_ENTRY_NUM (4096)

struct cp_in_entry {
	void * from_va;
	void *to_va;
	long length;
	short type;
	volatile uint8_t *descriptor;
	short status;
};

struct cp_in_queue {
	volatile unsigned int thread_read_index;
	volatile unsigned int write_index;
	struct cp_in_entry entries[DEFUALT_CP_IN_ENTRY_NUM];
};
