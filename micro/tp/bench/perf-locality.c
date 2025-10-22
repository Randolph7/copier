#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include "copier.h"

#define ENTRY_NUM 8192
#define PAGE_SIZE 4096

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("size num type\n");
		return 1;
	}
	const int size = atoi(argv[1]);
	const int num = atoi(argv[2]);
	const int type = atoi(argv[3]);
	struct mirco_report_data report;
	int ret;
	struct micro_cp_entry *entry;

	struct micro_cp_queue *queue = (struct micro_cp_queue *)malloc(sizeof(struct micro_cp_queue));
	memset((void*)queue, 0, sizeof(struct micro_cp_queue));
	for (int i = 0; i < num/4; i++) {
		char *from, *to;
		if(size > 4096)
			from = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);
		else
			from = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (from == MAP_FAILED) {
			perror("mmap failed");
			return 0;
		}
		from[0] = '1';
		if(size > 4096)
			to = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);
		else
			to = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (to == MAP_FAILED) {
			perror("mmap failed");
			return 0;
		}
		to[0] = '1';
		entry = &queue->entries[queue->write_index];
		entry->from = from;
		entry->to = to;
		entry->length = size;
		entry->from_offset = 0;
		entry->to_offset = 0;
		entry->dma = false;
		entry->type = MICRO_WORK;
		queue->write_index++;
	}
	for(int k = 0; k <3; k++)
		for (int i = 0; i < num/4; i++) {
			queue->entries[queue->write_index] = queue->entries[i];
			queue->write_index++;
		}

	entry = &queue->entries[queue->write_index];
	entry->type = MICRO_END;
	queue->write_index++;

	int fd = syscall(SYS_MICRO_CREATE, CORE, size, type, SEQ, 16, 10);
	struct micro_cp_queue *queue_final = (struct micro_cp_queue *)mmap(NULL, sizeof(struct micro_cp_queue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memcpy(queue_final, queue, sizeof(struct micro_cp_queue));
	queue_final->run = true;

	sleep(1);

	while (!queue_final->end)
		;
	ret = syscall(SYS_MICRO_REPORT, fd, &report);
	while (ret < 0)
		ret = syscall(SYS_MICRO_REPORT, fd, &report);

	unsigned long ns = report.end - report.start;
	unsigned long throughput = ((float)(num * size)) / ((float)(ns)) *1000;
	printf("75%% locality, time = %lu, throughput = %f GB\n", ns, ((float)throughput)/1000);
	for (int i = 0; i < num; i++) {
		entry = &queue->entries[i];
		munmap(entry->from, size);
		munmap(entry->to, size);
	}
	return 0;
}
