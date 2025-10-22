#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>

#define NUM_2M_PAGE 16

struct micro_cp_queue queue1;
struct micro_cp_queue queue2;

#define TASK_RR 1
#define FAIR 2
#define CGROUP13 3

int settings;

volatile unsigned long start;
volatile unsigned long queue1_end;
volatile unsigned long queue2_end;
struct copyer_ctx *ctx;

#define SLICE (32 * PAGE_SIZE)

static inline void prep_bufs(void)
{
	int i = 0, j, index;
	void *kva_src, *kva_dst;
	struct page *page_src, *page_dst;

	memset(&queue1, 0, sizeof(struct micro_cp_queue));
	memset(&queue2, 0, sizeof(struct micro_cp_queue));

	for (i = 0; i < NUM_2M_PAGE; i++) {
		page_src = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(PAGE_SIZE * 512));
		page_dst = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(PAGE_SIZE * 512));
		if (!page_src || !page_dst)
			printk("page alloc error\n");
		kva_src = page_address(page_src);
		kva_dst = page_address(page_dst);
		queue1.entries[i].page_from.page = page_src;
		queue1.entries[i].page_from.kva = kva_src;
		queue1.entries[i].page_to.page = page_dst;
		queue1.entries[i].page_to.kva = kva_dst;
		queue1.entries[i].from_offset = 0;
		queue1.entries[i].to_offset = 0;
		queue1.entries[i].length = PAGE_SIZE * 512;
		queue1.entries[i].descriptor = NULL;
		queue1.entries[i].dma = false;
		queue1.entries[i].type = MICRO_WORK;
	}
	queue1.write_index = NUM_2M_PAGE;
	queue1.thread_read_index = 0;

	for (i = 0; i < NUM_2M_PAGE; i++) {
		page_src = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(PAGE_SIZE * 512));
		page_dst = alloc_pages(GFP_TRANSHUGE | __GFP_COMP, get_order(PAGE_SIZE * 512));
		if (!page_src || !page_dst)
			printk("page alloc error\n");
		kva_src = page_address(page_src);
		kva_dst = page_address(page_dst);
		for (j = 0; j < 512; j++) {
			index = i * 512 + j;
			queue2.entries[index].page_from.page = page_src;
			queue2.entries[index].page_from.kva = kva_src;
			queue2.entries[index].page_to.page = page_dst;
			queue2.entries[index].page_to.kva = kva_dst;
			kva_src += PAGE_SIZE;
			kva_dst += PAGE_SIZE;
			queue2.entries[index].from_offset = 0;
			queue2.entries[index].to_offset = 0;
			queue2.entries[index].length = PAGE_SIZE;
			queue2.entries[index].descriptor = NULL;
			queue2.entries[index].dma = false;
			queue2.entries[index].type = MICRO_WORK;
		}
	}
	queue2.write_index = NUM_2M_PAGE * 512;
	queue2.thread_read_index = 0;
}

static inline void release_bufs(void)
{
	int i;
	struct micro_cp_entry *entry;
	for (i = 0; i < NUM_2M_PAGE; i++) {
		entry = &queue1.entries[i];
		__free_pages(entry->page_from.page, get_order(PAGE_SIZE * 512));
		__free_pages(entry->page_to.page, get_order(PAGE_SIZE * 512));
		entry = &queue2.entries[i * 512];
		__free_pages(entry->page_from.page, get_order(PAGE_SIZE * 512));
		__free_pages(entry->page_to.page, get_order(PAGE_SIZE * 512));
	}
}

static inline int micro_thread_background_fairness(void *ctx_void)
{
	unsigned int thread_read_index, usr_write_index, total_len, len_to_copy;
	struct copyer_ctx *ctx = (struct copyer_ctx *)ctx_void;
	struct micro_cp_entry *entry;

	KTHREAD_PREPARE_MM(ctx);
	if (settings == TASK_RR) {
		start = ktime_get_ns();
		while (!ctx->should_stop) {
			thread_read_index = queue1.thread_read_index;
			usr_write_index = queue1.write_index;
			if (thread_read_index != usr_write_index) {
				entry = &queue1.entries[thread_read_index];
				memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, entry->length);
				queue1.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
				if (thread_read_index == NUM_2M_PAGE - 1)
					queue1_end = ktime_get_ns();
			}
			thread_read_index = queue2.thread_read_index;
			usr_write_index = queue2.write_index;
			if (thread_read_index != usr_write_index) {
				entry = &queue2.entries[thread_read_index];
				memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, entry->length);
				queue2.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
				if (thread_read_index == NUM_2M_PAGE * 512 - 1)
					queue2_end = ktime_get_ns();
			}
		}
	} else if (settings == FAIR) {
		start = ktime_get_ns();
		while (!ctx->should_stop) {
			total_len = 0;
			while (total_len < SLICE) {
				thread_read_index = queue1.thread_read_index;
				usr_write_index = queue1.write_index;
				if (thread_read_index != usr_write_index) {
					entry = &queue1.entries[thread_read_index];
					if (total_len + entry->length > SLICE)
						len_to_copy = SLICE - total_len;
					else
						len_to_copy = entry->length;
					total_len += len_to_copy;
					memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, len_to_copy);
					if (len_to_copy == entry->length) {
						queue1.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
						if (thread_read_index == NUM_2M_PAGE - 1)
							queue1_end = ktime_get_ns();
					} else {
						entry->to_offset += len_to_copy;
						entry->from_offset += len_to_copy;
						entry->length -= len_to_copy;
					}
				} else
					break;
			}

			total_len = 0;
			while (total_len < SLICE) {
				thread_read_index = queue2.thread_read_index;
				usr_write_index = queue2.write_index;
				if (thread_read_index != usr_write_index) {
					entry = &queue2.entries[thread_read_index];
					if (total_len + entry->length > SLICE)
						len_to_copy = SLICE - total_len;
					else
						len_to_copy = entry->length;
					total_len += len_to_copy;
					memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, len_to_copy);
					if (len_to_copy == entry->length) {
						queue2.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
						if (thread_read_index == NUM_2M_PAGE * 512 - 1)
							queue2_end = ktime_get_ns();
					} else {
						entry->to_offset += len_to_copy;
						entry->from_offset += len_to_copy;
						entry->length -= len_to_copy;
					}

				} else
					break;
			}
		}
	} else if (settings == CGROUP13) {
		start = ktime_get_ns();
		while (!ctx->should_stop) {
			total_len = 0;
			while (total_len < 2 * SLICE) {
				thread_read_index = queue2.thread_read_index;
				usr_write_index = queue2.write_index;
				if (thread_read_index != usr_write_index) {
					entry = &queue2.entries[thread_read_index];
					if (total_len + entry->length > 2 * SLICE)
						len_to_copy = 2 * SLICE - total_len;
					else
						len_to_copy = entry->length;
					total_len += len_to_copy;
					memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, len_to_copy);
					if (len_to_copy == entry->length) {
						queue2.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
						if (thread_read_index == NUM_2M_PAGE * 512 - 1)
							queue2_end = ktime_get_ns();
					} else {
						entry->to_offset += len_to_copy;
						entry->from_offset += len_to_copy;
						entry->length -= len_to_copy;
					}

				} else
					break;
			}

			total_len = 0;
			while (total_len < SLICE) {
				thread_read_index = queue1.thread_read_index;
				usr_write_index = queue1.write_index;
				if (thread_read_index != usr_write_index) {
					entry = &queue1.entries[thread_read_index];
					if (total_len + entry->length > SLICE)
						len_to_copy = SLICE - total_len;
					else
						len_to_copy = entry->length;
					total_len += len_to_copy;
					memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, len_to_copy);
					if (len_to_copy == entry->length) {
						queue1.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
						if (thread_read_index == NUM_2M_PAGE - 1)
							queue1_end = ktime_get_ns();
					} else {
						entry->to_offset += len_to_copy;
						entry->from_offset += len_to_copy;
						entry->length -= len_to_copy;
					}

				} else
					break;
			}

			total_len = 0;
			while (total_len < 2 * SLICE) {
				thread_read_index = queue2.thread_read_index;
				usr_write_index = queue2.write_index;
				if (thread_read_index != usr_write_index) {
					entry = &queue2.entries[thread_read_index];
					if (total_len + entry->length > 2 * SLICE)
						len_to_copy = 2 * SLICE - total_len;
					else
						len_to_copy = entry->length;
					total_len += len_to_copy;
					memcpy(entry->page_to.kva + entry->to_offset, entry->page_from.kva + entry->from_offset, len_to_copy);
					if (len_to_copy == entry->length) {
						queue2.thread_read_index = (thread_read_index + 1) % MICRO_TEST_QUEUE_LEN;
						if (thread_read_index == NUM_2M_PAGE * 512 - 1)
							queue2_end = ktime_get_ns();
					} else {
						entry->to_offset += len_to_copy;
						entry->from_offset += len_to_copy;
						entry->length -= len_to_copy;
					}

				} else
					break;
			}
		}
	}

	KTHREAD_DROP_MM(ctx);

	return 0;
}

SYSCALL_DEFINE2(micro_fairness_create, int, core, int, type)
{
	// printk("call syscall_create_cp_thread, core = %d, queue_type = %d\n", core, queue_type);
	int ret = -1;

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);
	ctx->should_stop = 0;
	prep_bufs();
	queue1_end = 0;
	queue2_end = 0;

	settings = type;

	mmgrab(current->mm);
	ctx->mm = current->mm;
	if (core >= 0) {
		ret = -EINVAL;
		if (core >= nr_cpu_ids)
			goto err;
		if (!cpu_online(core))
			goto err;
		ctx->copyer_thread = kthread_create_on_cpu(micro_thread_background_fairness, (void *)ctx, core, "copyer-fairness");
	} else {
		ctx->copyer_thread = kthread_create(micro_thread_background_fairness, (void *)ctx, "copyer-fairness");
	}
	if (IS_ERR(ctx->copyer_thread)) {
		ret = PTR_ERR(ctx->copyer_thread);
		goto err;
	}
	wake_up_process(ctx->copyer_thread);

	return 0;
err:
	// kfree(ctx->queue);
	kfree(ctx);
	return ret;
}

SYSCALL_DEFINE1(micro_fairness_report, void *, report)
{
	// printk("call del_cp_thread\n");
	int ret;
	unsigned long time1, time2;

	if (!queue1_end || !queue2_end) {
		return -1;
	}

	time1 = queue1_end - start;
	time2 = queue2_end - start;
	ret = copy_to_user(report, &time1, sizeof(unsigned long));
	ret = copy_to_user(report + sizeof(unsigned long), &time2, sizeof(unsigned long));
	if (ret != 0)
		printk("report copy_to_user error!\n");

	ctx->should_stop = 1;
	kthread_stop(ctx->copyer_thread);
	ctx->copyer_thread = NULL;
	release_bufs();

	kfree(ctx);
	return 0;
}
