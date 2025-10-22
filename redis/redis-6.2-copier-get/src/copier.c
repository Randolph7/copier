#include "copier.h"
#include <bits/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define __USE_MISC
#include <unistd.h>
#include <stdatomic.h>

struct cp_queue * recv_cp_queue;

inline void smartInputDescriptorBufferGet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length)
{
	// printf("smartInputDescriptorBufferGet start, from_offset = %d, length = %d\n", from_offset, length);
	// struct timespec start, end;
	// clock_gettime(0, &start);
	long target = from_offset + length -1;
	// printf("smartBufferPtr->bufferDescriptor = %d target = %d, addr %lu\n", smartBufferPtr->bufferDescriptor, target, &smartBufferPtr->bufferDescriptor);
	while(smartBufferPtr->bufferDescriptor < target);
	// printf("start get\n");
}

inline void smartInputDescriptorBufferMemcpyAsyncSend(void *dst, void *src, struct smartInputDescriptorBuffer *smartBufferPtrSrc, size_t n)
{
	struct cp_in_queue* cp_in_queue = smartBufferPtrSrc->ctx->queue_in;
	struct cp_in_entry* cp_entry = &cp_in_queue->entries[cp_in_queue->write_index];
	cp_entry->type = TYPE_SIMPLE_COPY;
	cp_entry->length = n;
	cp_entry->from_va = src;
	cp_entry->to_va = dst;
	cp_in_queue->write_index = (cp_in_queue->write_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;
}

inline void smartInputDescriptorBufferMemcpyAsync(void *dst, void *src, struct smartInputDescriptorBuffer *smartBufferPtrSrc, size_t n)
{
	struct cp_queue* cp_queue = &smartBufferPtrSrc->ctx->queues_for_recv->queue;
	struct cp_entry* cp_entry = &cp_queue->entries[cp_queue->write_index];
	cp_entry->type = TYPE_SIMPLE_COPY;
	cp_entry->length = n;
	cp_entry->from_va = src;
	cp_entry->to_va = dst;
	cp_queue->write_index = (cp_queue->write_index + 1) % DEFUALT_CP_ENTRY_NUM;
}

inline void smartInputDescriptorBufferMemcpyNoRedirect(void *dst, void *src_io_base, struct smartInputDescriptorBuffer *smartBufferPtrSrc, unsigned long srcOffset, size_t n)
{
	unsigned int from_offset = srcOffset;
	const unsigned int target_from_offset = srcOffset + n - 1;
	void* to_addr = dst;
	unsigned int size;
	volatile unsigned long currentDescriptor;
	unsigned int sizeRecord = 0;
	while(smartBufferPtrSrc->bufferDescriptor < (int)srcOffset);
	while(1){
		currentDescriptor = smartBufferPtrSrc->bufferDescriptor;
		if (currentDescriptor >= target_from_offset){
			size = target_from_offset - from_offset + 1;
			sizeRecord += size;
			memcpy(to_addr, src_io_base + from_offset, size);
			break;
		}else {
			size = currentDescriptor - from_offset + 1;
			sizeRecord += size;
			memcpy(to_addr, src_io_base + from_offset, size);
			from_offset += size;
			to_addr += size;
		}
	}
}

// inline void smartInputDescriptorBufferMemcpyRedirect(void *dst_base, unsigned long dstOffset, void *src_io_base, struct smartInputDescriptorBuffer *smartBufferPtrSrc, unsigned long srcOffset, size_t n)
// {
// 	struct sync_queue * sync_queue = &smartBufferPtrSrc->ctx->queues_for_recv->sync_queue;
// 	unsigned int write_index = sync_queue->write_index;
// 	struct sync_entry* sync_entry = &sync_queue->entries[write_index];
// 	volatile int8_t redirect_descriptor = 0;
// 	sync_entry->action = SYNC_COPY_TODO_REDIRECT;
// 	sync_entry->sync_entry_descriptor = &redirect_descriptor;
// 	sync_entry->new_descriptor_buffer = NULL;
// 	sync_entry->redirect_to_va_offset_offset = dstOffset - srcOffset;
// 	sync_entry->redirect_va_base = dst_base;
// 	sync_entry->to_va_base = src_io_base;
// 	sync_entry->length = n;
// 	sync_entry->status = STATUS_WAITING;
// 	sync_entry->to_va = dst_base + dstOffset;	
	
// 	sync_queue->write_index = (write_index + 1)%DEFUALT_SYNC_ENTRY_NUM;

// 	unsigned int from_offset = srcOffset;
// 	const unsigned int target_from_offset = srcOffset + n - 1;
// 	void* to_addr = dst_base + dstOffset;
// 	unsigned int size;
// 	bool full = false;
// 	volatile unsigned long currentDescriptor;
// 	unsigned int sizeRecord = 0;
// 	while(smartBufferPtrSrc->bufferDescriptor < (int)srcOffset);
// 	while(!redirect_descriptor){
// 		currentDescriptor = smartBufferPtrSrc->bufferDescriptor;
// 		if (currentDescriptor >= target_from_offset){
// 			size = target_from_offset - from_offset + 1;
// 			sizeRecord += size;
// 			memcpy(to_addr, src_io_base + from_offset, size);
// 			full = true;
// 			break;
// 		} else {
// 			size = currentDescriptor - from_offset + 1;
// 			sizeRecord += size;
// 			memcpy(to_addr, src_io_base + from_offset, size);
// 			from_offset += size;
// 			to_addr += size;
// 		}
// 	}
// 	if(!full){
// 		size = smartBufferPtrSrc->bufferDescriptor - from_offset + 1;
// 		if(size)
// 			memcpy(to_addr, src_io_base + from_offset, size);
// 	}
// }

struct syncQueueCtx *mmapSyncQueue(int fd)
{
	struct syncQueueCtx *ctx = malloc(sizeof(struct syncQueueCtx));
	ctx->fd = fd;
	pthread_mutex_init(&ctx->mutex, NULL);
	ctx->queues_for_recv = (struct queues_for_recv *)mmap(NULL, sizeof(struct queues_for_recv), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	recv_cp_queue = &ctx->queues_for_recv->queue;
	if (!ctx->queues_for_recv) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

struct syncQueueCtx *mmapSyncQueueSend(int fd)
{
	struct syncQueueCtx *ctx = malloc(sizeof(struct syncQueueCtx));
	ctx->fd = fd;
	pthread_mutex_init(&ctx->mutex, NULL);
	ctx->queue_in = (struct cp_in_queue *)mmap(NULL, sizeof(struct cp_in_queue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (!ctx->queue_in) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void csync_all(void){
	// while(recv_cp_queue->thread_read_index != recv_cp_queue->write_index);
}

void destorySyncQueueCtx(struct syncQueueCtx *ctx)
{
	munmap(ctx->queues_for_recv, sizeof(struct sync_queue));
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

int createCpThread(int fd, int core, int type)
{
	return syscall(602, fd, core, type);
}

int delCpThread(int fd, int queue_fd, int type)
{
	return syscall(603, fd, queue_fd, type);
}

int bindCpThread(int sock_fd, int queue_fd, int type)
{
	return syscall(605, sock_fd, queue_fd, type);
}

inline size_t recvWithsmartInputDescriptorBufferRedis(long fd, void *__buf_base, size_t offset, size_t len, int flags,
						      struct smartInputDescriptorBuffer *buffer)
{
	while(recv_cp_queue->thread_read_index != recv_cp_queue->write_index);
	// unsigned long thisIOStartBlock = GET_BLOCK_N(offset);
	// if (offset % COPY_GRANULARITY) {
	// 	unsigned long privIOEndBlock = GET_BLOCK_N(buffer->current_input_end);
	// 	if (thisIOStartBlock == privIOEndBlock)
	// 		smartInputDescriptorBufferGet(__buf_base, buffer, thisIOStartBlock * COPY_GRANULARITY, COPY_GRANULARITY);
	// }
	// memset((void *)buffer->bufferDescriptor + thisIOStartBlock, 0, (MTU_MAX + COPY_GRANULARITY - 1) / COPY_GRANULARITY);
	buffer->bufferDescriptor = offset - 1;
	size_t recv_len = syscall(NR_RECVFROM_COPYER, fd, __buf_base + offset, __buf_base, len, flags, (void *)&(buffer->bufferDescriptor));
	// buffer->current_input_end = offset + recv_len - 1;
	return recv_len;
}
