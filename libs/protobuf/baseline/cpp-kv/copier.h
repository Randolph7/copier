#ifndef COPIER_H
#define COPIER_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <pthread.h>
#include <cstdint>

namespace copier {
#define COPY_GRANULARITY (1024 * 2)

#define GET_BLOCK_N(offset) ((offset) / COPY_GRANULARITY)

#define GET_DESCRIPTOR_ENTRY_ADDRESS(base, offset) (int8_t *)((char *)base + sizeof(int8_t) * GET_BLOCK_N(offset))
#define GET_DESCRIPTOR_ENTRY_ADDRESS_BY_BLOCK_NUM(base, blockN) (int8_t *)((char *)base + sizeof(int8_t) * blockN)

struct smartInputDescriptorBuffer {
	// volatile char bufferDescriptor[(1024 * 1024 * 16) / COPY_GRANULARITY];
	volatile long bufferDescriptor;
	size_t current_input_begin;
	size_t current_input_end;
	struct syncQueueCtx *ctx;
};
/* status in struct cp_entry and struct sync_entry */
#define STATUS_WAITING 1
// #define STATUS_DOING 2
#define STATUS_DONE 3
// #define STATUS_ABORTED 4
/* end status in struct cp_entry and struct sync_entry */

#define DEFUALT_CP_ENTRY_NUM 1024 * 1024

#define SYNC_COPY_TODO_ADVANCE 1
#define SYNC_COPY_TODO_ABORT 2
#define SYNC_COPY_TODO_REDIRECT 3

struct sync_entry {
	void *to_va_base;
	int to_va_offset;
	int length;

	short action;
	short status;

	// used only when action == SYNC_COPY_TODO_REDIRECT
	void *redirect_va;
	void *new_descriptor_buffer;
	int redirect_to_va_offset_offset; //current offset - original offset
};

#define DEFUALT_SYNC_ENTRY_NUM 8
// a queue that is too long does not comply with the semantics of sync

struct sync_queue {
	unsigned int size;
	unsigned int thread_read_index;
	unsigned int write_index;
	struct sync_entry entries[DEFUALT_SYNC_ENTRY_NUM];
};

#define ASYNC_THRESHOLD (PAGE_SIZE / 4)

struct syncQueueCtx {
	struct sync_queue *sync_queue;
	pthread_mutex_t mutex;
	int fd;
};

#define ERROR_QUEUE_FULL -1
#define NR_RECVFROM_COPYER 600

#define NR_RECVFROM_COPYER 600
#define NR_SENDTO_COPYER 606

#define MTU_MAX (1024 * 64)

/* type of queue */
#define QUEUE_TYPE_OUT 1
#define QUEUE_TYPE_IN 2
/* end type of queue */

#define sendWithCopier(fd, buf, n, flags) syscall(NR_SENDTO_COPYER, fd, buf, n, flags, NULL, 0)

struct syncQueueCtx *mmapSyncQueue(int fd);
void destorySyncQueueCtx(struct syncQueueCtx *ctx);
int createCpThread(int fd, int core, int type);
int delCpThread(int fd, int queue_fd, int type);
int bindCpThread(int sock_fd, int queue_fd, int type);
struct smartInputDescriptorBuffer *createsmartInputDescriptorBuffer(size_t length, void *(mallocFunc)(size_t __size));
void smartInputDescriptorBufferGet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length);
void smartInputDescriptorBufferBeforeSet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length);
void smartInputDescriptorBufferFree(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, void(freeFunc)(void *));

/* FOR REDIS */
size_t recvWithsmartInputDescriptorBufferRedis(long fd, void *__buf_base, size_t offset, size_t len, int flags, struct smartInputDescriptorBuffer *buffer);

}

#endif