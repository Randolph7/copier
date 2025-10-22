#include <linux/wait.h>
#include <linux/interval_tree_generic.h>
#include <linux/dmaengine.h>
#include <linux/hashtable.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

// #define MULTI_USER

/* type in struct cp_entry */
#define TYPE_RECV_SOCKET_DATA 1
#define TYPE_SOCKET_RELEASE_SKB 2
#define TYPE_RECV_SOCKET_DATA_TWO_BLOCK 3
#define TYPE_SEND_DATA 4
#define TYPE_SIMPLE_COPY 5

#define TYPE_BREAKDOWN_LOG 10
/* end type in struct cp_entry */

/* status in struct cp_entry and struct sync_entry */
#define STATUS_WAITING 1
// #define STATUS_DOING 2
#define STATUS_DONE 3
// #define STATUS_ABORTED 4
#define STATUS_SPARSE 5
/* end status in struct cp_entry and struct sync_entry */

/* block status */
#define COPIER_BLOCK_DONE 1
#define COPIER_BLOCK_REDIRECTED 2
/* end block status */

/* type of sync action */
#define SYNC_COPY_TODO_ADVANCE 1
#define SYNC_COPY_TODO_ABORT 2
#define SYNC_COPY_TODO_REDIRECT 3
/* end type of sync action */

/* magic number */
#define DEFUALT_CP_ENTRY_NUM (4096)
#define DEFUALT_CP_IN_ENTRY_NUM (4096)
#define DEFUALT_CP_BINDER_ENTRY_NUM (9900)

#define DEFUALT_SYNC_ENTRY_NUM (16)
// a queue that is too long does not comply with the semantics of sync
#define ASYNC_THRESHOLD (PAGE_SIZE / 4)
/* end magic number */

/* type of queue */
#define QUEUE_TYPE_OUT 1
#define QUEUE_TYPE_IN 2
#define QUEUE_TYPE_BINDER 3
#define QUEUE_TYPE_U2U 4
#define QUEUE_TYPE_MICRO 5
#define QUEUE_TYPE_IN_LAZY 6
/* end type of queue */

#define COPYER_WRAP(name) name##_copyer
#define COPYER_DRYRUN_WRAP(name) name##_copyer_dryrun
#define UNUSED(x) (void)(x)

#define KTHREAD_PREPARE_MM(ctx)                                                                                                                      \
	{                                                                                                                                            \
		kthread_use_mm(ctx->mm);                                                                                                             \
	}
#define KTHREAD_DROP_MM(ctx)                                                                                                                         \
	{                                                                                                                                            \
		kthread_unuse_mm(ctx->mm);                                                                                                           \
		mmdrop(ctx->mm);                                                                                                                     \
	}

/* FOR RECV */
typedef __u8 descriptor_entry;

#define COPY_GRANULARITY (1024 * 2)
#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

extern int copyout_copier(void __user *to, const void *from, size_t n);

struct cp_entry {
	union {
		void *from_va;
		struct page *page;
		struct sk_buff *skb;
		long io_length; //use when TYPE_BREAKDOWN_LOG
	};
	void *__user to_va_base;
	void *__user to_va;
	void *to_va_base_kernel;
	// struct cp_entry_interval_tree_node *interval_tree_node;
	// long to_va_offset;
	long from_offset;
	int length;
	int8_t status;
	int8_t type;
	union {
		void __user *descriptor_buffer;
		void *descriptor_buffer_kernel;
	};
};

struct u2u_cp_entry {
	void __user *from;
	void __user *to;
	void __user *base;
	unsigned long size;
	volatile uint16_t __user *descriptors;
	int8_t status;
	int granularity;
	int8_t lifo;
};

struct u2u_sync_entry {
	void __user *start_addr;
	unsigned long size;
};

struct u2u_sync_queue {
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	struct u2u_sync_entry entries[DEFUALT_SYNC_ENTRY_NUM] ____cacheline_aligned_in_smp;
};

struct u2u_cp_queue {
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	struct u2u_cp_entry entries[DEFUALT_CP_ENTRY_NUM] ____cacheline_aligned_in_smp;
};

struct queues_for_u2u {
	struct u2u_sync_queue sync_queue;
	struct u2u_cp_queue cp_queue;
};

enum micro_entry_type {
	// MICRO_START,
	MICRO_END,
	MICRO_WORK,
};

struct mirco_cached_page {
	struct page *page;
	void *kva;
	dma_addr_t dma_addr;
};

struct micro_cp_entry {
	enum micro_entry_type type;
	struct mirco_cached_page page_from;
	struct mirco_cached_page page_to;
	void *__user from;
	dma_addr_t dma_addr_from;
	void *__user to;
	dma_addr_t dma_addr_to;
	unsigned int from_offset;
	unsigned int to_offset;
	unsigned int length;
	int16_t *descriptor;
	bool dma;
	struct dma_chan *chan;
	dma_cookie_t cookie;
	bool dma_dual_chan;
	struct dma_chan *chan2;
	dma_cookie_t cookie2;
};

// #define MICRO_TEST_TOTAL_PAGE (8192)
// #define MICRO_TEST_TOTAL_LEN (MICRO_TEST_TOTAL_PAGE * PAGE_SIZE)
#define MICRO_TEST_QUEUE_LEN (8192 + 2)

enum micro_queue_type {
	ERMS,
	DMA_SINGLE_CHAN,
	DMA_FULL_CHAN,
	AVX,
	COPIER,
	COPIER_NOCACHE,
};

struct mirco_report_data {
	unsigned long start;
	unsigned long end;
	unsigned long entry_num;
	unsigned long entry_length;
};

#define HASH_TABLE_BITS 12

struct va2dma_cache_node {
	void *va;
	dma_addr_t dma;
	void *kva;
	struct page **pages;
	struct hlist_node node;
	enum dma_data_direction dir;
	u64 length;
};

struct micro_cp_queue {
	volatile bool run;
	volatile bool end;
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	struct micro_cp_entry entries[MICRO_TEST_QUEUE_LEN] ____cacheline_aligned_in_smp;
	enum micro_queue_type type;
	struct mirco_report_data report_data;
	// struct hlist_head va2pa_cache[1 << HASH_TABLE_BITS];
	// struct hlist_head pa2dma_cache[1 << HASH_TABLE_BITS];
	struct hlist_head va2dma_cache[1 << HASH_TABLE_BITS];
	int cache_pool_index;
	struct va2dma_cache_node cache_poll[2 * MICRO_TEST_QUEUE_LEN];
};

struct cp_queue {
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	struct cp_entry entries[DEFUALT_CP_ENTRY_NUM] ____cacheline_aligned_in_smp;
	// spinlock_t locks[DEFUALT_CP_ENTRY_NUM];
	// struct cp_entry prev_half_entry_cache;
	// bool prev_half_entry_cache_used;
	// struct cp_entry prev_release_entry_cache;
	// bool prev_release_entry_cache_used;
	// struct rb_root_cached cp_entry_index_interval_tree_root;
	// rwlock_t interval_tree_lock;
	unsigned long last_used_base;
	unsigned long last_used_desriptor;
	void *last_mapped_base_addr;
	void *last_mapped_descriptor_addr;
};
// __attribute__((aligned(PAGE_SIZE)));

struct sync_entry {
	void __user *to_va_base;
	void __user *to_va;
	int length;

	short action;
	short status;

	// used only when action == SYNC_COPY_TODO_REDIRECT
	void __user *redirect_va_base;
	void __user *new_descriptor_buffer;
	void *sync_entry_descriptor;
	int redirect_to_va_offset_offset; //current offset - original offset
};

struct sync_queue {
	volatile unsigned int thread_read_index ____cacheline_aligned_in_smp;
	volatile unsigned int write_index ____cacheline_aligned_in_smp;
	struct sync_entry entries[DEFUALT_SYNC_ENTRY_NUM] ____cacheline_aligned_in_smp;
};

struct cp_entry_interval_tree_node {
	struct rb_node rb;
	unsigned long start; /* Start of interval */
	unsigned long last; /* Last location _in_ interval */
	unsigned long __subtree_last;
	long entry_index_in_queue;
};

#define GET_BLOCK_N(offset) (offset / COPY_GRANULARITY)

#define GET_DESCRIPTOR_ENTRY_ADDRESS(base, offset) (base + sizeof(descriptor_entry) * GET_BLOCK_N(offset))

inline struct cp_entry_interval_tree_node *add_interval_to_tree(struct cp_queue *queue, long start, int length, long entry_index);
inline void delete_interval_from_tree(struct cp_queue *queue, struct cp_entry_interval_tree_node *node);

struct cp_entry *get_half_entry_pos(struct cp_queue *q, unsigned int *prev_index);
int after_add_half_entry(struct cp_queue *q, unsigned int prev_index, unsigned long entry_length);
struct cp_entry *get_full_entry_pos(struct cp_queue *q, unsigned int *index);
void after_add_full_entry(struct cp_queue *q, unsigned int index, unsigned long start_addr, unsigned long entry_length);
struct cp_entry *get_release_entry_pos(struct cp_queue *q, unsigned int *index);
void after_add_release_entry(struct cp_queue *q, unsigned int index);
int flush_queue_cache(struct cp_queue *q);
int flush_queue_cache_and_add_log_entry(struct cp_queue *q, long io_length);

/* END FOR RECV */

/* FOR SEND */
extern int copyin(void *to, const void __user *from, size_t n);

struct cp_in_entry {
	union {
		void *__user from_va;
		void *from_va_kernel;
	};
	void *to_va;
	long length;
	short type;
	volatile uint8_t *descriptor;
	short status;
};

#define GLOBAL_DESCRIPTOR_SIZE (16)
#define COPIER_HASH_NODE_POLL_SIZE (64)

struct umm_vmap_cache_node {
	unsigned long uva;
	unsigned long kva;
	int page_num;
	struct hlist_node node;
};

struct cp_in_queue {
	volatile unsigned int thread_read_index;
	volatile unsigned int write_index;
	struct cp_in_entry entries[DEFUALT_CP_IN_ENTRY_NUM];
	unsigned long last_used_base;
	void *last_mapped_base_addr;
	volatile int descriptor_index;
	volatile int hash_node_index;
	struct hlist_head ht[16];
	volatile uint8_t global_descriptor[GLOBAL_DESCRIPTOR_SIZE];
	struct umm_vmap_cache_node hash_nodes[COPIER_HASH_NODE_POLL_SIZE];
};

#ifdef MULTI_USER
#define add_entry_to_cp_in_queue(q, to, from, n, descriptor_)                                                                                        \
	({                                                                                                                                           \
		unsigned int write_index = q->write_index;                                                                                           \
		if (q->entries[write_index].status == STATUS_WAITING) {                                                                              \
			printk("full in add_entry_to_cp_in_queue\n");                                                                                \
			0;                                                                                                                           \
		} else {                                                                                                                             \
			q->entries[write_index].descriptor = descriptor_;                                                                            \
			q->entries[write_index].from_va_kernel = (unsigned long)from - (unsigned long)q->last_used_base + q->last_mapped_base_addr;  \
			q->entries[write_index].to_va = to;                                                                                          \
			q->entries[write_index].length = n;                                                                                          \
			q->entries[write_index].status = STATUS_WAITING;                                                                             \
		}                                                                                                                                    \
		q->write_index = (write_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;                                                                        \
		1;                                                                                                                                   \
	})
#else
#define add_entry_to_cp_in_queue(q, to, from, n, descriptor_)                                                                                        \
	({                                                                                                                                           \
		unsigned int write_index = q->write_index;                                                                                           \
		if (q->entries[write_index].status == STATUS_WAITING) {                                                                              \
			printk("full in add_entry_to_cp_in_queue\n");                                                                                \
			0;                                                                                                                           \
		} else {                                                                                                                             \
			q->entries[write_index].descriptor = descriptor_;                                                                            \
			q->entries[write_index].from_va = from;                                                                                      \
			q->entries[write_index].type = TYPE_SEND_DATA;                                                                               \
			q->entries[write_index].to_va = to;                                                                                          \
			q->entries[write_index].length = n;                                                                                          \
			q->entries[write_index].status = STATUS_WAITING;                                                                             \
		}                                                                                                                                    \
		q->write_index = (write_index + 1) % DEFUALT_CP_IN_ENTRY_NUM;                                                                        \
		1;                                                                                                                                   \
	})
#endif
/* END FOR SEND */

/* FOR BINDER */
enum binder_entry_type { CP_MID, CP_START, CP_END, CP_ONCE, CP_DELAY };
struct binder_entry {
	enum binder_entry_type type;
	struct page *page;
	pgoff_t pgoff;
	void __user *from;
	int size;
};

struct binder_queue {
	volatile unsigned int binder_read_index;
	volatile unsigned int binder_write_index;
	struct binder_entry entries[DEFUALT_CP_ENTRY_NUM];
};

/* END FORBINDER */
struct cp_thread_whole_system_ctx {
	volatile int should_stop;
};

struct queues_for_recv {
	// struct sync_queue sync_queue;
	struct cp_queue queue;
};

struct copyer_ctx {
	union {
		// for recv
		struct queues_for_recv *queues_for_recv;
		//for send
		struct cp_in_queue *queue_in;
		//for binder
		struct binder_queue *b_queue;
		//for u2u
		struct queues_for_u2u *queues_for_u2u;
		//for micro
		struct micro_cp_queue *micro_queue;
	};
	union {
		int queue_index;
		struct task_struct *copyer_thread;
	};
	struct mm_struct *mm;
	int should_wake_up;
	volatile int should_stop;
	int queue_type;
	int (*thread_func)(void *);
};

struct multi_user_recv_queue_struct {
	struct queues_for_recv *queues_for_recv;
	struct mm_struct *mm;
	int valid;
};

struct multi_user_send_queue_struct {
	struct cp_in_queue cp_in_queue;
	int valid;
};

inline int recv_prep_cp_thread(struct copyer_ctx *ctx);
inline void recv_bind(int userfd, int threadfd);
inline void recv_prep_destory(int fd, struct copyer_ctx *ctx);

inline int send_prep_cp_thread(struct copyer_ctx *ctx);
inline int send_prep_cp_thread_lazy(struct copyer_ctx *ctx);
inline void send_bind(int userfd, int threadfd);
inline void send_prep_destory(int fd, struct copyer_ctx *ctx);

inline int u2u_prep_cp_thread(struct copyer_ctx *ctx);
inline void u2u_bind(int userfd, int threadfd);
inline void u2u_prep_destory(int fd, struct copyer_ctx *ctx);

int thread_background_cp_whole_system(void *ctx_void);
int thread_background_cp_in_whole_system(void *ctx_void);
