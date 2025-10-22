#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "copier.h"
#include "../config.h"
#include <x86intrin.h>

// extern unsigned long submit_cycles;
// extern unsigned long submit_time;

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("array size -- segment size\n");
		return 1;
	}
	const int granularity = 1024;
	const int block = granularity / sizeof(ARRAY_TYPE);
	const int prewarmRounds = 10;
	const int evaluationRounds = 1000;
	const long arrayLength = atoi(argv[1]);
	const int size = arrayLength / sizeof(ARRAY_TYPE);

	ARRAY_TYPE *from, *to, *dst;
	posix_memalign((void **)&from, 4096, arrayLength);
	posix_memalign((void **)&to, 4096, arrayLength);
	posix_memalign((void **)&dst, 4096, arrayLength);

	long *timeRecords = malloc(evaluationRounds * sizeof(long));

	int queuefd = createCpThread();
	struct queues_for_u2u *queues = mmapQueues(queuefd);
	volatile uint16_t *descriptors = malloc(sizeof(uint16_t) * arrayLength / granularity);

	srand((unsigned int)time(NULL));

	for (int i = 0; i < size; i++) {
		from[i] = rand();
		to[i] = 0;
	}

	struct timespec start, end;
	ARRAY_TYPE max = 0;

	for (int r = 0; r < prewarmRounds; r++) {
		max = 0;
		memset((void*)descriptors, 0, sizeof(uint16_t) * arrayLength / granularity);
		for (int offset = 0; offset < arrayLength; offset += 64){
			_mm_clflush((char *)to + offset);
			_mm_clflush((char *)from + offset);
		}
		_mm_mfence();

		clock_gettime(CLOCK_MONOTONIC, &start);

		memcpyAsync(to, from, &(queues->cp_queue), descriptors, arrayLength, granularity);

		for (int b = 0; b < size / block; b++) {
			smartBufferGetAlignedOnlyWait(to, b, granularity, descriptors);
			for (int i = b * block; i < (b + 1) * block; i += 16) {
				if (to[i] > max)
					max = to[i];
				if (to[i + 1] > max)
					max = to[i];
				if (to[i + 2] > max)
					max = to[i];
				if (to[i + 3] > max)
					max = to[i];
				if (to[i + 4] > max)
					max = to[i];
				if (to[i + 5] > max)
					max = to[i];
				if (to[i + 6] > max)
					max = to[i];
				if (to[i + 7] > max)
					max = to[i];
				if (to[i + 8] > max)
					max = to[i];
				if (to[i + 9] > max)
					max = to[i];
				if (to[i + 10] > max)
					max = to[i];
				if (to[i + 11] > max)
					max = to[i];
				if (to[i + 12] > max)
					max = to[i];
				if (to[i + 13] > max)
					max = to[i];
				if (to[i + 14] > max)
					max = to[i];
				if (to[i + 15] > max)
					max = to[i];
			}
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		timeRecords[r] = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
	}

	for (int r = 0; r < evaluationRounds; r++) {
		max = 0;
		for (int offset = 0; offset < arrayLength; offset += 64){
			_mm_clflush((char *)to + offset);
			_mm_clflush((char *)from + offset);
		}
		_mm_mfence();
		memset((void*)descriptors, 0, sizeof(uint16_t) * arrayLength / granularity);
		clock_gettime(CLOCK_MONOTONIC, &start);

		memcpyAsync(to, from, &(queues->cp_queue), descriptors, arrayLength, granularity);

		for (int b = 0; b < size / block; b++) {
			smartBufferGetAlignedOnlyWait(to, b, granularity, descriptors);
			for (int i = b * block; i < (b + 1) * block; i += 16) {
				if (to[i] > max)
					max = to[i];
				if (to[i + 1] > max)
					max = to[i];
				if (to[i + 2] > max)
					max = to[i];
				if (to[i + 3] > max)
					max = to[i];
				if (to[i + 4] > max)
					max = to[i];
				if (to[i + 5] > max)
					max = to[i];
				if (to[i + 6] > max)
					max = to[i];
				if (to[i + 7] > max)
					max = to[i];
				if (to[i + 8] > max)
					max = to[i];
				if (to[i + 9] > max)
					max = to[i];
				if (to[i + 10] > max)
					max = to[i];
				if (to[i + 11] > max)
					max = to[i];
				if (to[i + 12] > max)
					max = to[i];
				if (to[i + 13] > max)
					max = to[i];
				if (to[i + 14] > max)
					max = to[i];
				if (to[i + 15] > max)
					max = to[i];
			}
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		timeRecords[r] = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
	}

	float avg = 0;
	unsigned long sum = 0, old_sum;

	// Knuth's incremental average algorithm
	for (size_t i = 0; i < evaluationRounds; ++i) {
		old_sum = sum;
		sum += timeRecords[i];
		if (sum < old_sum)
			printf("sum overflow\n");
	}
	avg = (float)sum / evaluationRounds;

	printf("Avg = %.2f ns\n", avg);

	munmapQueues(queues);
	delCpThread(queuefd);

	return 0;
}