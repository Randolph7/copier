extern volatile long *getBufferDescriptor(void);
extern volatile int *getAsyncRecv(void);
extern void **getSyncQueue(void);

struct sync_entry {
	void *to_va_base;
	void *to_va;
	int length;
	short action;
	volatile short status;
};

struct sync_queue {
	volatile unsigned int thread_read_index __attribute__((aligned(64)));
	volatile unsigned int write_index __attribute__((aligned(64)));
	struct sync_entry entries[16] __attribute__((aligned(64)));
};
