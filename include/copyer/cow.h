#include <linux/mm_types.h>
#include <copyer/copyer.h>

#define COW_ENTRY_NUM 512

struct cow_entry {
	struct page *src;
	struct page *dst;
	volatile uint8_t *status;
	bool is_huge_page;
};

struct cow_cp_queue {
	struct cow_entry entries[COW_ENTRY_NUM];
	volatile int write_index;
	volatile int thread_read_index;
	struct dma_chan *chan1;
	struct dma_chan *chan2;
	struct device *dma_dev;
	volatile bool should_stop;
};

#define TOTAL_PAGE 512
#define DMA_PER_CHAN_RATE 3 / 16
#define AVX_RATE 10 / 16

#define DMA_PER_CHAN_PAGE (TOTAL_PAGE * DMA_PER_CHAN_RATE)
#define AVX_PAGE (TOTAL_PAGE * AVX_RATE)

inline void asyc_copy_huge_page(struct page *src, struct page *dst, volatile uint8_t *status);
inline void asyc_copy_page(struct page *src, struct page *dst, volatile uint8_t *status);