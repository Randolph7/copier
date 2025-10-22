#include <linux/spinlock_types.h>
#include <copyer/copier.h>

#define MAX_COPIER_CLIENT_NUM 16

#define COPIER_HASH_NODE_POLL_SIZE 4096

struct at_cache_node {
	u64 uva;
	void *kva;
	int page_num;
	struct page **pages;
	struct hlist_node node;
};

struct at_cache {
	struct hlist_head ht[16];
	struct at_cache_node nodes_pool[COPIER_HASH_NODE_POLL_SIZE];
	u32 pool_index;
};

struct copier_scheduler_node {
	struct rb_node node;
	u32 cp_length;
	u16 queue_index;
};

struct copier_ctx_sys_service {
	struct copier_ctx *queue_ctx[MAX_COPIER_CLIENT_NUM];
	volatile int queue_count;
	struct task_struct *copier_thread;
	volatile int should_stop;
	struct rb_root index_root;
	bool kernel_queue_blocked[MAX_COPIER_CLIENT_NUM];
	bool user_queue_blocked[MAX_COPIER_CLIENT_NUM];
	struct cp_queue *user_cp_queue[MAX_COPIER_CLIENT_NUM];
	struct cp_queue_kernel *kernel_cp_queue[MAX_COPIER_CLIENT_NUM];
	u32 barrier_index[MAX_COPIER_CLIENT_NUM];
	struct mm_struct *mm[MAX_COPIER_CLIENT_NUM];
	struct at_cache at_cache[MAX_COPIER_CLIENT_NUM];
	struct copier_scheduler_node scheduler_node[MAX_COPIER_CLIENT_NUM];
	spinlock_t scheduler_index_lock;
};
