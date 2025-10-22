#include <linux/types.h>
#include <linux/kernel.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct smartInputDescriptorBuffer {
	union {
		struct {
			volatile char bufferDescriptor[1024];
			// volatile long bufferDescriptor;
			size_t current_input_begin;
			size_t current_input_end;
		};
		volatile int64_t descriptor;
	};
};

struct copier_param {
	void *buf_base;
	int copier_fd;
	void *descriptor_buffer;
};

enum {
	TYPE_RECV_SOCKET_DATA = 1,
	TYPE_SOCKET_RELEASE_SKB,
	TYPE_SEND_DATA,
	TYPE_SIMPLE_COPY,
	BARRIER_START,
	BARRIER_END,
};

enum {
	STATUS_WAITING = 1,
	STATUS_DONE = 3,
};

#define QUEUE_LEN 512
#define COPYLENGTH 1024

#define CACHE_LINE_SIZE 64

struct cp_task {
	void *from_va_base;
	void *from_va;
	void *to_va_base;
	void *to_va;
	long from_offset;
	int length;
	int8_t status;
	int8_t type;
	void *descriptor;
	void *skb;
};

struct sync_task {
	void *start_addr;
	unsigned long size;
	int8_t status;
};

struct cp_queue {
	struct cp_task tasks[QUEUE_LEN];
	volatile unsigned int thread_read_index __attribute__((aligned(CACHE_LINE_SIZE)));
	volatile unsigned int write_index __attribute__((aligned(CACHE_LINE_SIZE)));
	uint8_t recycle_count;
};

struct sync_queue {
	struct sync_task tasks[QUEUE_LEN];
	volatile unsigned int thread_read_index __attribute__((aligned(CACHE_LINE_SIZE)));
	volatile unsigned int write_index __attribute__((aligned(CACHE_LINE_SIZE)));
};

#define NR_RECVFROM_COPYER 600
#define NR_SENDTO_COPYER 606
#define NR_create_cp_service 607
#define NR_create_cp_queue 608
#define NR_release_cp_queue 609
#define NR_del_cp_service 611

#define MTU_MAX (1024 * 64)

void prep_copier_queue(void);
void del_copier_queue(void);
size_t asend(int fd, const void *buf, size_t n, int flags);
void *_amemcpy_no_sync(void *dst, void* dst_base, const void *src, size_t n);
size_t arecv(long fd, void *__buf_base, size_t offset, size_t len, int flags, struct smartInputDescriptorBuffer *buffer);
void _csync(struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length);

int create_copier_service(int core);
void del_copier_service();
