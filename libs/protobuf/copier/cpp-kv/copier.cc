#include "copier.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>

namespace copier{

void smartInputDescriptorBufferGet(void *io_base, struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length)
{
	// printf("smartInputDescriptorBufferGet start, from_offset = %d, length = %d\n", from_offset, length);
	// struct timespec start, end;
	// clock_gettime(0, &start);
	unsigned int target = from_offset + length -1;
	// printf("smartBufferPtr->bufferDescriptor = %d target = %d, addr %lu\n", smartBufferPtr->bufferDescriptor, target, &smartBufferPtr->bufferDescriptor);
	while(smartBufferPtr->bufferDescriptor < target);
	// printf("start get\n");
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

struct syncQueueCtx *mmapSyncQueue(int fd)
{
	struct syncQueueCtx *ctx = (struct syncQueueCtx *)malloc(sizeof(struct syncQueueCtx));
	ctx->fd = fd;
	pthread_mutex_init(&ctx->mutex, NULL);
	ctx->sync_queue = (struct sync_queue *)mmap(NULL, sizeof(struct sync_queue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (!ctx->sync_queue) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void destorySyncQueueCtx(struct syncQueueCtx *ctx)
{
	munmap(ctx->sync_queue, sizeof(struct sync_queue));
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

size_t recvWithsmartInputDescriptorBufferRedis(long fd, void *__buf_base, size_t offset, size_t len, int flags,
						      struct smartInputDescriptorBuffer *buffer)
{
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
}
