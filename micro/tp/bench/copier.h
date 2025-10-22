
#include <stdint.h>
#include <sys/types.h>

#define CACHE_LINE_SIZE 64
#define MICRO_TEST_QUEUE_LEN (8192 + 2)

enum micro_queue_type {
	ERMS,
	DMA_SINGLE_CHAN,
	DMA_FULL_CHAN,
	AVX,
	COPIER,
	COPIER_NOCACHE,
	COPIER_LARGE,
	COPIER_LARGE_NOCACHE
};

enum micro_entry_type {
	// MICRO_START,
	MICRO_END,
	MICRO_WORK,
};

struct mirco_cached_page {
	void *page;
	void *kva;
	u_int64_t dma_addr;
};

struct micro_cp_entry {
	enum micro_entry_type type;
	struct mirco_cached_page page_from;
	struct mirco_cached_page page_to;
	void *from;
	u_int64_t dma_addr_from;
	void *to;
	u_int64_t dma_addr_to;
	unsigned int from_offset;
	unsigned int to_offset;
	unsigned int length;
	int16_t *descriptor;
	bool dma;
	struct dma_chan *chan;
	int32_t cookie;
	bool dma_dual_chan;
	struct dma_chan *chan2;
	int32_t cookie2;
};

struct mirco_report_data {
	unsigned long start;
	unsigned long end;
	unsigned long entry_num;
	unsigned long entry_length;
};

struct micro_cp_queue {
	volatile bool run;
	volatile bool end;
	volatile unsigned int thread_read_index __attribute__((aligned(CACHE_LINE_SIZE)));
	volatile unsigned int write_index __attribute__((aligned(CACHE_LINE_SIZE)));
	struct micro_cp_entry entries[MICRO_TEST_QUEUE_LEN] __attribute__((aligned(CACHE_LINE_SIZE)));
};

#define SYS_MICRO_CREATE 703
#define SYS_MICRO_REPORT 704
#define CORE 2

#define SEQ 1
