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
#include <fcntl.h>

int queue_fd;
struct cp_queue *cp_queue;
struct sync_queue *sync_queue;

// size_t virtual_to_physical(size_t addr)
// {
//     int fd = open("/proc/self/pagemap", O_RDONLY);
//     if(fd < 0)
//     {
//         printf("open '/proc/self/pagemap' failed!\n");
//         return 0;
//     }
//     size_t pagesize = getpagesize();
//     size_t offset = (addr / pagesize) * sizeof(uint64_t);
//     if(lseek(fd, offset, SEEK_SET) < 0)
//     {
//         printf("lseek() failed!\n");
//         close(fd);
//         return 0;
//     }
//     uint64_t info;
//     if(read(fd, &info, sizeof(uint64_t)) != sizeof(uint64_t))
//     {
//         printf("read() failed!\n");
//         close(fd);
//         return 0;
//     }
//     if((info & (((uint64_t)1) << 63)) == 0)
//     {
//         printf("page is not present!\n");
//         close(fd);
//         return 0;
//     }
//     size_t frame = info & ((((uint64_t)1) << 55) - 1);
//     size_t phy = frame * pagesize + addr % pagesize;
//     close(fd);
//     return phy;
// }

void prep_copier_queue(void)
{
	queue_fd = syscall(NR_create_cp_queue);
	cp_queue = (struct cp_queue *)mmap(NULL, sizeof(struct cp_queue) + sizeof(struct sync_queue), PROT_READ | PROT_WRITE, MAP_SHARED, queue_fd, 0);
	sync_queue = (struct sync_queue *)(cp_queue + 1);
}

void del_copier_queue(void)
{
	syscall(NR_release_cp_queue, queue_fd);
	munmap(cp_queue, sizeof(struct cp_queue) + sizeof(struct sync_queue));
}

size_t asend(int fd, const void *buf, size_t n, int flags)
{
	/* printf("asend, from %lu, len %lu\n", (unsigned long) buf, (unsigned long) n); */
	return syscall(NR_SENDTO_COPYER, fd, buf, n, flags, queue_fd);
}

void *_amemcpy_no_sync(void *dst, void *src_base, const void *src, size_t n)
{
	// printf("amemcpy, from %lu, to %lu, len %lu\n", (unsigned long) src, (unsigned long) dst, (unsigned long) n);
	unsigned int write_index;
	struct cp_task *cp_task;

	write_index = cp_queue->write_index;
	cp_task = &cp_queue->tasks[write_index];

	cp_task->from_va_base = (void *)src_base;
	cp_task->from_va = (void *)src;
	cp_task->to_va_base = dst;
	cp_task->to_va = dst;
	cp_task->from_offset = 0;
	cp_task->length = n;
	cp_task->status = STATUS_WAITING;
	cp_task->type = TYPE_SIMPLE_COPY;
	cp_task->descriptor = NULL;

	cp_queue->write_index = (write_index + 1) % QUEUE_LEN;

	return dst;
}

inline void _csync(struct smartInputDescriptorBuffer *smartBufferPtr, unsigned long from_offset, unsigned long length)
{
	// uint64_t phy = virtual_to_physical((uint64_t)(&(smartBufferPtr->descriptor)));
	while (smartBufferPtr->descriptor < (int64_t)(from_offset + length - 1)) {
		// printf("%lu phy %lu %ld\n", (uint64_t)&(smartBufferPtr->descriptor), phy, smartBufferPtr->descriptor);
	};
	// uint32_t start_block = from_offset / COPYLENGTH;
	// uint32_t end_block = (from_offset + length - 1) / COPYLENGTH;
	// uint32_t i;
	// for (i = start_block; i <= end_block; i++) {
	// 	while (smartBufferPtr->bufferDescriptor[i] != 1)
	// 		;
	// }
}

inline size_t arecv(long fd, void *__buf_base, size_t offset, size_t len, int flags, struct smartInputDescriptorBuffer *buffer)
{
	// unsigned long thisIOStartBlock = offset / COPYLENGTH;
	struct copier_param param;
	// if (offset % COPYLENGTH) {
	// 	unsigned long privIOEndBlock = buffer->current_input_end / COPYLENGTH;
	// 	if (thisIOStartBlock == privIOEndBlock) {
	// 		while (buffer->bufferDescriptor[thisIOStartBlock] != 1)
	// 			;
	// 	}
	// }
	param.buf_base = __buf_base;
	param.copier_fd = queue_fd;
	param.descriptor_buffer = &buffer->descriptor;
	// memset((void *)buffer->bufferDescriptor + offset / COPYLENGTH, 0, MTU_MAX / COPYLENGTH);
	buffer->descriptor = offset - 1;
	while(cp_queue->write_index != cp_queue->thread_read_index);
	size_t recv_len = syscall(NR_RECVFROM_COPYER, fd, __buf_base + offset, len, flags, &param);
	// buffer->current_input_end = offset + recv_len - 1;
	return recv_len;
}

int create_copier_service(int core)
{
	return syscall(NR_create_cp_service, core);
}

void del_copier_service()
{
	syscall(NR_del_cp_service);
}
