#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <x86intrin.h>
#include "../config.h"

void *____memcpy(void *dest, const void *src, size_t n)
{
	long d0, d1, d2;
	asm volatile("rep ; movsq\n\t"
		     "movq %4,%%rcx\n\t"
		     "rep ; movsb\n\t"
		     : "=&c"(d0), "=&D"(d1), "=&S"(d2)
		     : "0"(n >> 3), "g"(n & 7), "1"(dest), "2"(src)
		     : "memory");

	return dest;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("array size\n");
		return 1;
	}
	const int arrayLength = atoi(argv[1]);
	const int prewarmRounds = 50;
	const int evaluationRounds = 50;
	const long size = arrayLength / sizeof(ARRAY_TYPE);

	ARRAY_TYPE *from, *to, *dst;
	posix_memalign((void **)&from, 4096, arrayLength);
	posix_memalign((void **)&to, 4096, arrayLength);
	posix_memalign((void **)&dst, 4096, arrayLength);

	long *timeRecords = malloc(evaluationRounds * sizeof(long));

	srand((unsigned int)time(NULL));

	for (int i = 0; i < size; i++) {
		from[i] = rand();
		to[i] = 0;
	}

	struct timespec start, end;

	for (int r = 0; r < prewarmRounds; r++) {
		ARRAY_TYPE max = 0;

		for (int offset = 0; offset < arrayLength; offset += 64){
			_mm_clflush((char *)to + offset);
			_mm_clflush((char *)from + offset);
		}
		_mm_mfence();
		
		clock_gettime(CLOCK_MONOTONIC, &start);

		____memcpy(to, from, arrayLength);

		for (int i = 0; i < size; i += 16) {
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

		clock_gettime(CLOCK_MONOTONIC, &end);
		timeRecords[r] = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
	}

	for (int r = 0; r < evaluationRounds; r++) {
		ARRAY_TYPE max = 0;
		
		for (int offset = 0; offset < arrayLength; offset += 64){
			_mm_clflush((char *)to + offset);
			_mm_clflush((char *)from + offset);
		}
		_mm_mfence();

		clock_gettime(CLOCK_MONOTONIC, &start);

		____memcpy(to, from, arrayLength);

		for (int i = 0; i < size; i += 16) {
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

	return 0;
}