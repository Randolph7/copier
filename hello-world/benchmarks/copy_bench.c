#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "../copier.h"

#define DEFAULT_SIZE_BYTES (1UL << 30) /* 1 GiB */
#define DEFAULT_ROUNDS 5
#define DEFAULT_GRANULARITY 4096

enum copy_mode {
	MODE_MEMCPY = 0,
	MODE_ASYNC = 1,
};

struct summary_stats {
	double avg_ns;
	double min_ns;
	double max_ns;
	double throughput_gib_s;
};

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [--mode memcpy|async] [--size <bytes|{K,M,G}>] "
		"[--rounds N] [--granularity bytes]\n",
		prog);
}

static size_t parse_size_arg(const char *arg)
{
	char *end = NULL;
	errno = 0;
	unsigned long long value = strtoull(arg, &end, 10);
	if (errno != 0 || end == arg)
		return 0;

	unsigned long long multiplier = 1;
	if (*end != '\0') {
		if (*(end + 1) != '\0')
			return 0;
		switch (tolower(*end)) {
		case 'k':
			multiplier = 1024ULL;
			break;
		case 'm':
			multiplier = 1024ULL * 1024ULL;
			break;
		case 'g':
			multiplier = 1024ULL * 1024ULL * 1024ULL;
			break;
		default:
			return 0;
		}
	}

	unsigned long long result = value * multiplier;
	if (result == 0 || result > SIZE_MAX)
		return 0;
	return (size_t)result;
}

static inline long timespec_diff_ns(const struct timespec *start, const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) * 1000000000LL + (end->tv_nsec - start->tv_nsec);
}

static void compute_stats(const long *records, int count, size_t size_bytes, struct summary_stats *stats)
{
	double sum = 0.0;
	double min = (double)records[0];
	double max = (double)records[0];

	for (int i = 0; i < count; ++i) {
		double val = (double)records[i];
		sum += val;
		if (val < min)
			min = val;
		if (val > max)
			max = val;
	}

	stats->avg_ns = sum / count;
	stats->min_ns = min;
	stats->max_ns = max;

	double bytes_per_round = (double)size_bytes;
	double seconds = stats->avg_ns / 1e9;
	double gib = bytes_per_round / (1024.0 * 1024.0 * 1024.0);
	stats->throughput_gib_s = gib / seconds;
}

int main(int argc, char **argv)
{
	const char *mode_arg = "memcpy";
	size_t size_bytes = DEFAULT_SIZE_BYTES;
	int rounds = DEFAULT_ROUNDS;
	size_t granularity = DEFAULT_GRANULARITY;

	static struct option long_opts[] = {
		{ "mode", required_argument, NULL, 'm' },
		{ "size", required_argument, NULL, 's' },
		{ "rounds", required_argument, NULL, 'r' },
		{ "granularity", required_argument, NULL, 'g' },
		{ NULL, 0, NULL, 0 }
	};

	while (1) {
		int opt = getopt_long(argc, argv, "", long_opts, NULL);
		if (opt == -1)
			break;
		switch (opt) {
		case 'm':
			mode_arg = optarg;
			break;
		case 's':
			size_bytes = parse_size_arg(optarg);
			break;
		case 'r':
			rounds = atoi(optarg);
			break;
		case 'g':
			granularity = parse_size_arg(optarg);
			break;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	if (size_bytes == 0 || rounds <= 0 || granularity == 0) {
		print_usage(argv[0]);
		return 1;
	}

	enum copy_mode mode;
	if (strcmp(mode_arg, "memcpy") == 0) {
		mode = MODE_MEMCPY;
	} else if (strcmp(mode_arg, "async") == 0) {
		mode = MODE_ASYNC;
	} else {
		fprintf(stderr, "Unknown mode '%s'\n", mode_arg);
		return 1;
	}

	if (size_bytes % granularity != 0) {
		fprintf(stderr, "Size (%zu) must be divisible by granularity (%zu)\n",
			size_bytes, granularity);
		return 1;
	}

	if (granularity > INT_MAX) {
		fprintf(stderr, "Granularity (%zu) exceeds INT_MAX\n", granularity);
		return 1;
	}

	size_t blocks = size_bytes / granularity;
	if (blocks == 0) {
		fprintf(stderr, "Configuration results in zero blocks\n");
		return 1;
	}
	if (blocks > INT_MAX) {
		fprintf(stderr, "Block count (%zu) exceeds INT_MAX\n", blocks);
		return 1;
	}

	ARRAY_TYPE *src = NULL;
	ARRAY_TYPE *dst = NULL;
	if (posix_memalign((void **)&src, 4096, size_bytes) != 0 ||
	    posix_memalign((void **)&dst, 4096, size_bytes) != 0) {
		perror("posix_memalign");
		return 1;
	}

	srand((unsigned int)time(NULL));
	for (size_t i = 0; i < size_bytes / sizeof(ARRAY_TYPE); ++i) {
		src[i] = (ARRAY_TYPE)rand();
		dst[i] = 0;
	}

	volatile uint16_t *descriptors = NULL;
	uint16_t *descriptor_buf = NULL;
	int queue_fd = -1;
	struct queues_for_u2u *queues = NULL;

	if (mode == MODE_ASYNC) {
		size_t desc_bytes = blocks * sizeof(uint16_t);
		if (posix_memalign((void **)&descriptor_buf, 64, desc_bytes) != 0) {
			perror("posix_memalign descriptors");
			return 1;
		}
		memset(descriptor_buf, 0, blocks * sizeof(uint16_t));
		descriptors = descriptor_buf;

		queue_fd = createCpThread();
		if (queue_fd < 0) {
			perror("createCpThread");
			return 1;
		}

		queues = mmapQueues(queue_fd);
		if (queues == MAP_FAILED) {
			perror("mmapQueues");
			delCpThread(queue_fd);
			return 1;
		}
	}

	long *records = calloc((size_t)rounds, sizeof(long));
	if (!records) {
		perror("calloc");
		return 1;
	}

	printf("Mode       : %s\n", mode_arg);
	printf("Size       : %.2f GiB (%zu bytes)\n",
	       (double)size_bytes / (1024.0 * 1024.0 * 1024.0), size_bytes);
	printf("Rounds     : %d\n", rounds);
	printf("Granularity: %zu bytes\n", granularity);

	for (int r = 0; r < rounds; ++r) {
		struct timespec start, end;

		if (mode == MODE_ASYNC) {
			memset((void *)descriptors, 0, blocks * sizeof(uint16_t));
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &start);
		if (mode == MODE_MEMCPY) {
			memcpy(dst, src, size_bytes);
		} else {
			memcpyAsync(dst, src, &(queues->cp_queue), descriptors, size_bytes,
				    (int)granularity);
			for (size_t b = 0; b < blocks; ++b)
				smartBufferGetAlignedOnlyWait(dst, (int)b, granularity, descriptors);
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &end);

		long delta = timespec_diff_ns(&start, &end);
		records[r] = delta;
		double seconds = (double)delta / 1e9;
		double gib = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
		double throughput = gib / seconds;
		printf("Run %3d : %ld ns (%.3f s, %.3f GiB/s)\n", r + 1, delta, seconds, throughput);
	}

	struct summary_stats stats;
	compute_stats(records, rounds, size_bytes, &stats);

	printf("Average   : %.2f ms\n", stats.avg_ns / 1e6);
	printf("Min / Max : %.2f ms / %.2f ms\n", stats.min_ns / 1e6, stats.max_ns / 1e6);
	printf("Throughput: %.2f GiB/s\n", stats.throughput_gib_s);

	printf("{\"mode\":\"%s\",\"size_bytes\":%zu,\"rounds\":%d,"
	       "\"granularity_bytes\":%zu,"
	       "\"avg_ns\":%.2f,\"min_ns\":%.2f,\"max_ns\":%.2f,"
	       "\"throughput_gib_s\":%.4f}\n",
	       mode_arg, size_bytes, rounds, granularity,
	       stats.avg_ns, stats.min_ns, stats.max_ns, stats.throughput_gib_s);

	free(records);
	free(src);
	free(dst);
	free(descriptor_buf);
	if (queues && queues != MAP_FAILED)
		munmapQueues(queues);
	if (queue_fd >= 0)
		delCpThread(queue_fd);

	return 0;
}

