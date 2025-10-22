#include <linux/types.h>
#include <linux/kernel.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef int8_t descriptorEntry;

#define COPY_GRANULARITY (1024 * 2)

#define GET_BLOCK_N(offset) ((offset) / COPY_GRANULARITY)

#define GET_DESCRIPTOR_ENTRY_ADDRESS(base, offset) (descriptorEntry *)((char *)base + sizeof(descriptorEntry) * GET_BLOCK_N(offset))
#define GET_DESCRIPTOR_ENTRY_ADDRESS_BY_BLOCK_NUM(base, blockN) (descriptorEntry *)((char *)base + sizeof(descriptorEntry) * blockN)

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
#define TYPE_SIMPLE_COPY 5

#define DEFUALT_CP_ENTRY_NUM (4096)

#define SYNC_COPY_TODO_ADVANCE 1
#define SYNC_COPY_TODO_ABORT 2
#define SYNC_COPY_TODO_REDIRECT 3

#define CACHE_LINE_SIZE 64

struct sync_entry {
	void *to_va_base;
	void *to_va;
	int length;

	short action;
	short status;

	// used only when action == SYNC_COPY_TODO_REDIRECT
	void *redirect_va_base;
	void *new_descriptor_buffer;
	void *sync_entry_descriptor;
	int redirect_to_va_offset_offset; //current offset - original offset
};

struct cp_entry {
	union {
		void *from_va;
		struct page *page;
		struct sk_buff *skb;
		long io_length; //use when TYPE_BREAKDOWN_LOG
	};
	void * to_va_base;
	void * to_va;
	void *to_va_base_kernel;
	// struct cp_entry_interval_tree_node *interval_tree_node;
	// long to_va_offset;
	long from_offset;
	int length;
	int8_t status;
	int8_t type;
	union {
		void *descriptor_buffer;
		void *descriptor_buffer_kernel;
	};
};

struct cp_queue {
	volatile unsigned int thread_read_index __attribute__((aligned(CACHE_LINE_SIZE)));
	volatile unsigned int write_index __attribute__((aligned(CACHE_LINE_SIZE)));
	struct cp_entry entries[DEFUALT_CP_ENTRY_NUM] __attribute__((aligned(CACHE_LINE_SIZE)));
	unsigned long last_used_base;
	unsigned long last_used_desriptor;
	void *last_mapped_base_addr;
	void *last_mapped_descriptor_addr;
};

#define DEFUALT_SYNC_ENTRY_NUM 16
// a queue that is too long does not comply with the semantics of sync

struct sync_queue {
	volatile unsigned int thread_read_index __attribute__((aligned(CACHE_LINE_SIZE)));
	volatile unsigned int write_index __attribute__((aligned(CACHE_LINE_SIZE)));
	struct sync_entry entries[DEFUALT_SYNC_ENTRY_NUM] __attribute__((aligned(CACHE_LINE_SIZE)));
};

struct queues_for_recv {
	// struct sync_queue sync_queue;
	struct cp_queue queue;
};

#define ASYNC_THRESHOLD (PAGE_SIZE / 4)

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

struct syncQueueCtx {
	union {
		struct queues_for_recv *queues_for_recv;
		struct cp_in_queue* queue_in;
	};
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
struct syncQueueCtx *mmapSyncQueueSend(int fd);
void destorySyncQueueCtx(struct syncQueueCtx *ctx);
int createCpThread(int fd, int core, int type);
int delCpThread(int fd, int queue_fd, int type);
int bindCpThread(int sock_fd, int queue_fd, int type);
struct smartInputDescriptorBuffer *createsmartInputDescriptorBuffer(size_t length, void *(mallocFunc)(size_t __size));
void smartInputDescriptorBufferGet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length);
void smartInputDescriptorBufferBeforeSet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length);
void smartInputDescriptorBufferFree(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, void(freeFunc)(void *));
void smartInputDescriptorBufferMemcpy(void *dst_io_base, struct smartInputDescriptorBuffer *smartBufferPtrDst, unsigned long dstOffset, void *src_io_base,
				      struct smartInputDescriptorBuffer *smartBufferPtrSrc, unsigned long srcOffset, size_t n, bool useSrcAfterMove);
void smartInputDescriptorBufferMemcpyNoRedirect(void *dst, void *src_io_base, struct smartInputDescriptorBuffer *smartBufferPtrSrc, unsigned long srcOffset, size_t n);
void smartInputDescriptorBufferMemcpyRedirect(void *dst_base, unsigned long dstOffset, void *src_io_base, struct smartInputDescriptorBuffer *smartBufferPtrSrc, unsigned long srcOffset, size_t n);
void smartInputDescriptorBufferMemcpyAsync(void *dst, void *src, struct smartInputDescriptorBuffer *smartBufferPtrSrc, size_t n);
void smartInputDescriptorBufferMemcpyAsyncSend(void *dst, void *src, struct smartInputDescriptorBuffer *smartBufferPtrSrc, size_t n);
// void smartInputDescriptorBufferMemmove(struct smartInputDescriptorBuffer *smartBufferPtrDst, unsigned long dstOffset, struct smartInputDescriptorBuffer *smartBufferPtrSrc,
// 			     unsigned long srcOffset, size_t n, bool useSrcAfterMove);

/* FOR REDIS */
size_t recvWithsmartInputDescriptorBufferRedis(long fd, void *__buf_base, size_t offset, size_t len, int flags, struct smartInputDescriptorBuffer *buffer);
void csync_all(void);
