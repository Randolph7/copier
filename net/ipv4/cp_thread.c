#include "asm/fpu/api.h"
#include "linux/slab.h"
#include "linux/types.h"
#include <linux/spinlock.h>
#include <net/icmp.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <copyer/copyer.h>
// #include "cp_memlog.h"

extern struct copyer_ctx *recv_ctx;
extern struct copyer_ctx *send_ctx;

static inline int syscall_create_cp_thread(int core, int queue_type)
{
	// printk("call syscall_create_cp_thread, core = %d, queue_type = %d\n", core, queue_type);
	struct copyer_ctx *ctx;
	int fd;
	int ret;
	int (*binder_prep_cp_thread)(struct copyer_ctx * ctx);

	ctx = kmalloc(sizeof(struct copyer_ctx), GFP_KERNEL);
	ctx->queue_type = queue_type;
	switch (queue_type) {
	case QUEUE_TYPE_OUT:
		fd = recv_prep_cp_thread(ctx);
		recv_ctx = ctx;
		break;
	case QUEUE_TYPE_IN:
		fd = send_prep_cp_thread(ctx);
		send_ctx = ctx;
		break;
	case QUEUE_TYPE_U2U:
		fd = u2u_prep_cp_thread(ctx);
		break;
	case QUEUE_TYPE_BINDER:
		binder_prep_cp_thread = (void *)kallsyms_lookup_name("binder_prep_cp_thread");
		if (binder_prep_cp_thread)
			fd = binder_prep_cp_thread(ctx);
		else
			printk("Error! binder_prep_cp_thread not found!\n");
		break;
	case QUEUE_TYPE_IN_LAZY:
		fd = send_prep_cp_thread_lazy(ctx);
		send_ctx = ctx;
		break;
	default:
		printk("unknown queue type!\n");
	}
	ctx->should_wake_up = 0;
	ctx->should_stop = 0;
	mmgrab(current->mm);
	ctx->mm = current->mm;
	if (core >= 0) {
		ret = -EINVAL;
		if (core >= nr_cpu_ids)
			goto err;
		if (!cpu_online(core))
			goto err;
		ctx->copyer_thread = kthread_create_on_cpu(ctx->thread_func, (void *)ctx, core, "copyer-wt");
	} else {
		ctx->copyer_thread = kthread_create(ctx->thread_func, (void *)ctx, "copyer-wt");
	}
	if (IS_ERR(ctx->copyer_thread)) {
		ret = PTR_ERR(ctx->copyer_thread);
		goto err;
	}
	wake_up_process(ctx->copyer_thread);

	return fd;
err:
	// kfree(ctx->queue);
	kfree(ctx);
	return ret;
}

SYSCALL_DEFINE3(create_cp_thread, int, fd, __u64, core, int, type)
{
	void (*binder_bind)(int userfd, int threadfd);
	int t_fd = syscall_create_cp_thread(core, type);
	if (t_fd > 0) {
		switch (type) {
		case QUEUE_TYPE_OUT:
			recv_bind(fd, t_fd);
			break;
		case QUEUE_TYPE_IN:
		case QUEUE_TYPE_IN_LAZY:
			send_bind(fd, t_fd);
			break;
		case QUEUE_TYPE_U2U:
			u2u_bind(fd, t_fd);
			break;
		case QUEUE_TYPE_BINDER:
			binder_bind = (void *)kallsyms_lookup_name("binder_bind");
			if (binder_bind)
				binder_bind(fd, t_fd);
			else
				printk("Error! binder_bind not found!\n");
			break;
		default:
			printk("unknown queue type!\n");
		}
	}
	// printk("return fd = %d\n", t_fd);
	return t_fd;
}

SYSCALL_DEFINE3(bind_cp_thread, int, fd, int, thread_fd, int, type)
{
	// printk("[copyer] bind queue fd %d to socket fd %d\n", queue_fd, fd);
	// printk("bind fd = %d\n", thread_fd);
	void (*binder_bind)(int userfd, int threadfd);
	if (thread_fd > 0) {
		switch (type) {
		case QUEUE_TYPE_OUT:
			recv_bind(fd, thread_fd);
			break;
		case QUEUE_TYPE_IN:
		case QUEUE_TYPE_IN_LAZY:
			send_bind(fd, thread_fd);
			break;
		case QUEUE_TYPE_U2U:
			u2u_bind(fd, thread_fd);
			break;
		case QUEUE_TYPE_BINDER:
			binder_bind = (void *)kallsyms_lookup_name("binder_bind");
			if (binder_bind)
				binder_bind(fd, thread_fd);
			else
				printk("Error! binder_bind not found!\n");
			break;
		default:
			printk("unknown queue type!\n");
		}
	}
	return thread_fd;
}

SYSCALL_DEFINE3(del_cp_thread, int, fd, int, queue_fd, int, type)
{
	// printk("call del_cp_thread\n");
	struct copyer_ctx *ctx;
	void (*binder_prep_destory)(int fd, struct copyer_ctx *ctx);
	struct file *q_file = fget(queue_fd);

	if (!q_file) {
		printk("[copyer] NO SUCH FILE");
		return -EFAULT;
	}
	ctx = (struct copyer_ctx *)(q_file->private_data);
	ctx->should_stop = 1;
	kthread_stop(ctx->copyer_thread);
	ctx->copyer_thread = NULL;
	switch (type) {
	case QUEUE_TYPE_OUT:
		recv_prep_destory(fd, ctx);
		break;
	case QUEUE_TYPE_IN:
	case QUEUE_TYPE_IN_LAZY:
		send_prep_destory(fd, ctx);
		break;
	case QUEUE_TYPE_U2U:
		u2u_prep_destory(fd, ctx);
		break;
	case QUEUE_TYPE_BINDER:
		binder_prep_destory = (void *)kallsyms_lookup_name("binder_prep_destory");
		if (binder_prep_destory)
			binder_prep_destory(fd, ctx);
		else
			printk("Error! binder_prep_destory not found!\n");
		break;
	default:
		printk("unknown queue type!\n");
	}
	kfree(ctx);
	return 0;
}

SYSCALL_DEFINE2(test_avx_dma, void __user *, buf, int, compare)
{
	uint64_t start, end;
	dma_cap_mask_t mask_memcpy;
	struct dma_chan *chan0;
	uint64_t int64_buf[4];

	if (!compare) {
		asm volatile("wbinvd" : : : "memory");
		start = rdtsc();
		kernel_fpu_begin();
		end = rdtsc();
		int64_buf[0] = end - start;

		asm volatile("wbinvd" : : : "memory");
#ifdef __AVX512F__
		asm volatile("vpxord  %%zmm0,  %%zmm0, %%zmm0\n"
			     "vpxord  %%zmm1,  %%zmm1, %%zmm1\n"
			     "vpxord  %%zmm2,  %%zmm2, %%zmm2\n"
			     "vpxord  %%zmm3,  %%zmm3, %%zmm3\n"
			     "vpxord  %%zmm4,  %%zmm4, %%zmm4\n"
			     "vpxord  %%zmm5,  %%zmm5, %%zmm5\n"
			     "vpxord  %%zmm6,  %%zmm6, %%zmm6\n"
			     "vpxord  %%zmm7,  %%zmm7, %%zmm7\n"
			     "vpxord  %%zmm8,  %%zmm8, %%zmm8\n"
			     "vpxord  %%zmm9,  %%zmm9, %%zmm9\n"
			     "vpxord  %%zmm10, %%zmm10, %%zmm10\n"
			     "vpxord  %%zmm11, %%zmm11, %%zmm11\n"
			     "vpxord  %%zmm12, %%zmm12, %%zmm12\n"
			     "vpxord  %%zmm13, %%zmm13, %%zmm13\n"
			     "vpxord  %%zmm14, %%zmm14, %%zmm14\n"
			     "vpxord  %%zmm15, %%zmm15, %%zmm15\n"
			     "vpxord  %%zmm16, %%zmm16, %%zmm16\n"
			     "vpxord  %%zmm17, %%zmm17, %%zmm17\n"
			     "vpxord  %%zmm18, %%zmm18, %%zmm18\n"
			     "vpxord  %%zmm19, %%zmm19, %%zmm19\n"
			     "vpxord  %%zmm20, %%zmm20, %%zmm20\n"
			     "vpxord  %%zmm21, %%zmm21, %%zmm21\n"
			     "vpxord  %%zmm22, %%zmm22, %%zmm22\n"
			     "vpxord  %%zmm23, %%zmm23, %%zmm23\n"
			     "vpxord  %%zmm24, %%zmm24, %%zmm24\n"
			     "vpxord  %%zmm25, %%zmm25, %%zmm25\n"
			     "vpxord  %%zmm26, %%zmm26, %%zmm26\n"
			     "vpxord  %%zmm27, %%zmm27, %%zmm27\n"
			     "vpxord  %%zmm28, %%zmm28, %%zmm28\n"
			     "vpxord  %%zmm29, %%zmm29, %%zmm29\n"
			     "vpxord  %%zmm30, %%zmm30, %%zmm30\n"
			     "vpxord  %%zmm31, %%zmm31, %%zmm31\n"
			     :
			     :);
#else
		__asm__ __volatile__("vzeroall" : : :);
#endif
		start = rdtsc();
		dma_cap_zero(mask_memcpy);
		dma_cap_set(DMA_MEMCPY, mask_memcpy);
		chan0 = dma_request_chan_by_mask(&mask_memcpy);
		end = rdtsc();
		int64_buf[2] = end - start;

		asm volatile("wbinvd" : : : "memory");
		start = rdtsc();
		kernel_fpu_end();
		end = rdtsc();
		int64_buf[1] = end - start;
	}
	asm volatile("wbinvd" : : : "memory");
	int64_buf[3] = rdtsc();

	copy_to_user(buf, int64_buf, 4 * sizeof(uint64_t));

	return 0;
}

SYSCALL_DEFINE0(test_empty_syscall)
{
	return 0;
}

SYSCALL_DEFINE3(test_addr_transfer, void __user *, buffer, long, length, void __user *, time_buf)
{
	int i;
	dma_cap_mask_t mask_memcpy;
	struct dma_chan *chan0;
	struct device *device;
	int page_num;
	uint64_t start, end, uva_page_t, page_kva_t, pa_dma_t, dma1_t, dma2_t;
	struct page **pages;
	dma_addr_t *dma_addrs;
	void **kva_addrs;
	struct dma_async_tx_descriptor *chan_desc;
	dma_cookie_t* dma_cookies;

	dma_cap_zero(mask_memcpy);
	dma_cap_set(DMA_MEMCPY, mask_memcpy);
	chan0 = dma_request_chan_by_mask(&mask_memcpy);
	device = chan0->device->dev;

	start = rdtsc();
	page_num = (length > PAGE_SIZE) ? length / PAGE_SIZE : 1;
	pages = kmalloc_array(page_num, sizeof(struct page *), GFP_KERNEL);
	get_user_pages_fast((unsigned long)buffer, page_num, 0, pages);
	end = rdtsc();
	uva_page_t = end - start;

	kva_addrs = kmalloc_array(page_num, sizeof(void *), GFP_KERNEL);
	dma_cookies = kmalloc_array(page_num/2, sizeof(dma_cookie_t), GFP_KERNEL);

	start = rdtsc();
	for (i = 0; i < page_num; i++)
		kva_addrs[i] = kmap(pages[i]);
	end = rdtsc();
	page_kva_t = end - start;

	dma_addrs = kmalloc_array(page_num, sizeof(dma_addr_t), GFP_KERNEL);

	start = rdtsc();
	for (i = 0; i < page_num; i++)
		dma_addrs[i] = dma_map_page(device, pages[i], 0, PAGE_SIZE, DMA_FROM_DEVICE);
	end = rdtsc();
	pa_dma_t = end - start;

	start = rdtsc();
	for (i = 0; i < page_num/2; i++){
		chan_desc = dmaengine_prep_dma_memcpy(chan0, dma_addrs[i], dma_addrs[page_num - 1 - i], PAGE_SIZE, DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
		if (IS_ERR_OR_NULL(chan_desc)) { /* handle error */
			printk("chan_desc error\n");
		}
		dma_cookies[i] = dmaengine_submit(chan_desc);
		dma_async_issue_pending(chan0);
	}
	end = rdtsc();
	dma1_t = end - start;

	start = rdtsc();
	for (i = 0; i < page_num/2; i++){
		dma_async_is_tx_complete(chan0, dma_cookies[i], NULL, NULL);
	}
	end = rdtsc();
	dma2_t = end - start;

	copy_to_user(time_buf, &uva_page_t, sizeof(uint64_t));
	copy_to_user(time_buf + sizeof(uint64_t), &page_kva_t, sizeof(uint64_t));
	copy_to_user(time_buf + 2 * sizeof(uint64_t), &pa_dma_t, sizeof(uint64_t));
	copy_to_user(time_buf + 3 * sizeof(uint64_t), &dma1_t, sizeof(uint64_t));
	copy_to_user(time_buf + 4 * sizeof(uint64_t), &dma2_t, sizeof(uint64_t));

	for (i = 0; i < page_num; i++)
		kunmap(pages[i]);
	for (i = 0; i < page_num; i++)
		dma_unmap_page(device, pages[i], PAGE_SIZE, DMA_FROM_DEVICE);
	kfree(pages);
	kfree(kva_addrs);
	kfree(dma_addrs);
	return 0;
}
