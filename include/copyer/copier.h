#include <linux/wait.h>
#include <linux/interval_tree_generic.h>
#include <linux/dmaengine.h>
#include <linux/hashtable.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

enum {
	TYPE_RECV_SOCKET_DATA = 1,
	TYPE_SOCKET_RELEASE_SKB,
	TYPE_SEND_DATA,
	TYPE_SIMPLE_COPY,
	BARRIER_START,
	BARRIER_END,
};

enum {
	STATUS_WAITING = 1,
	STATUS_DONE = 3,
};

#define QUEUE_LEN 512
#define COPYLENGTH 1024

#define COPYER_WRAP(name) name##_copyer
#define COPYER_DRYRUN_WRAP(name) name##_copyer_dryrun
#define UNUSED(x) (void)(x)

struct cp_task {
	union {
		struct {
			void *from_va_base;
			void *from_va;
		};
		struct page *from_page;
		struct {
			unsigned int index; // used for barrier task
			unsigned int recycle_count;
		};
	};
	void *to_va_base;
	void *to_va;
	long from_offset;
	int length;
	int8_t status;
	int8_t type;
	union {
		void *descriptor;
		volatile uint8_t *descriptor_byte;
	};
	struct sk_buff *skb;
};

struct sync_task {
	void __user *start_addr;
	unsigned long size;
	int8_t status;
};

struct cp_queue {
	struct cp_task tasks[QUEUE_LEN];
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	u8 recycle_count;
};

struct cp_queue_kernel {
	struct cp_task tasks[QUEUE_LEN];
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	spinlock_t lock;
};

struct sync_queue {
	struct sync_task tasks[QUEUE_LEN];
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
};

struct process_queues {
	struct cp_queue user_cp_queue;
	struct sync_queue sync_queue;
	struct cp_queue_kernel kernel_cp_queue;
};

struct copier_ctx {
	struct process_queues *queues;
	union {
		int queue_index;
		struct task_struct *copier_thread;
	};
	struct mm_struct *mm;
	volatile int should_stop;
};

inline void add_start_barrier(struct cp_queue_kernel *q, struct cp_queue *uq);
inline void add_end_barrier(struct cp_queue_kernel *q);

struct copier_param {
	void *buf_base;
	int copier_fd;
	void *descriptor_buffer;
};
