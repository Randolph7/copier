
#include <copyer/copyer.h>

inline int add_cached_half_cp_entry_to_queue(struct cp_queue *q)
{
	// printk("add_cached_half_cp_entry_to_queue\n");
	unsigned int write_index = q->write_index;
	unsigned int next_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;
	if (q->entries[write_index].status == STATUS_WAITING)
		return -1;
	// printk("flush1 offset = %d entry to queue, index = %d, type = %d\n", q->prev_half_entry_cache->to_va_offset, write_index, q->prev_half_entry_cache->type);
	q->entries[write_index] = q->prev_half_entry_cache;
	q->entries[write_index].interval_tree_node =
		add_interval_to_tree(q, (unsigned long)q->prev_half_entry_cache.to_va_base + q->prev_half_entry_cache.to_va_offset,
				     q->prev_half_entry_cache.length, write_index);
	// printk("add_cached_half_cp_entry_to_queue entry info: from %ld to %ld ; interval info: from %ld to %ld, addr = %ld\n",
	//        (unsigned long)q->entries[write_index].to_va_base + q->entries[write_index].to_va_offset,
	//        (unsigned long)q->entries[write_index].to_va_base + q->entries[write_index].to_va_offset + q->entries[write_index].length - 1,
	//        q->entries[write_index].interval_tree_node->start, q->entries[write_index].interval_tree_node->last,
	//        (unsigned long)q->entries[write_index].interval_tree_node);

	q->write_index = next_index;

	// printk("to free prev_half_entry_cache\n");
	q->prev_half_entry_cache_used = false;

	if (q->prev_release_entry_cache_used) {
		if (q->entries[next_index].status == STATUS_WAITING)
			return -1;
		// printk("flush2 release entry to queue, index = %d, status = %d\n", write_index, q->prev_release_entry_cache->status);
		q->entries[next_index] = q->prev_release_entry_cache;
		q->write_index = (next_index + 1) % DEFUALT_CP_ENTRY_NUM;

		// printk("to free prev_release_entry_cache\n");
		// kfree(q->prev_release_entry_cache);
		q->prev_release_entry_cache_used = false;
	}
	return 0;
}

/* add type = TYPE_RECV_SOCKET_DATA half entry related interface */

// Notes: Currently only consider single thread. No race condition
inline struct cp_entry *get_half_entry_pos(struct cp_queue *q, unsigned int *prev_index)
{
	unsigned int write_index, write_index_next;
	if (!q->prev_half_entry_cache_used) {
		// printk("add offset = %d entry to cache, type = %d\n", entry->to_va_offset, entry->type);
		// printk("add to cache\n");
		q->prev_half_entry_cache_used = true;
		return &(q->prev_half_entry_cache);
	}
	// printk("add to queue\n");
	write_index = q->write_index;
	write_index_next = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;
	if (q->entries[write_index].status == STATUS_WAITING || q->entries[write_index_next].status == STATUS_WAITING) {
		// spin_unlock(&q->locks[write_index]);
		return NULL;
	}

	// printk("flush3 offset = %d entry to queue, offset = %d\n", q->prev_half_entry_cache->to_va_offset, write_index);
	q->entries[write_index] = q->prev_half_entry_cache;
	q->entries[write_index].type = TYPE_RECV_SOCKET_DATA_TWO_BLOCK;
	// printk("flush4 offset = %d entry to queue, offset = %d\n", entry->to_va_offset, write_index_next);
	q->prev_half_entry_cache_used = false;
	*prev_index = write_index;
	return &(q->entries[write_index_next]);
}

inline int after_add_half_entry(struct cp_queue *q, unsigned int prev_index, unsigned long entry_length)
{
	unsigned int next_index = (prev_index + 2) % DEFUALT_CP_ENTRY_NUM;
	q->entries[prev_index].interval_tree_node =
		add_interval_to_tree(q, (unsigned long)q->entries[prev_index].to_va_base + q->entries[prev_index].to_va_offset,
				     q->entries[prev_index].length + entry_length, prev_index);
	// printk("after_add_half_entry entry info: from %ld to %ld ; interval info: from %ld to %ld, addr = %ld\n",
	//        (unsigned long)q->entries[prev_index].to_va_base + q->entries[prev_index].to_va_offset,
	//        (unsigned long)q->entries[prev_index].to_va_base + q->entries[prev_index].to_va_offset + q->entries[prev_index].length - 1,
	//        q->entries[prev_index].interval_tree_node->start, q->entries[prev_index].interval_tree_node->last,
	//        (unsigned long)q->entries[prev_index].interval_tree_node);

	q->write_index = next_index;

	if (q->prev_release_entry_cache_used) {
		if (q->entries[next_index].status == STATUS_WAITING)
			return -1;
		q->entries[next_index] = q->prev_release_entry_cache;
		q->write_index = (next_index + 1) % DEFUALT_CP_ENTRY_NUM;

		q->prev_release_entry_cache_used = false;
	}
	return 0;
}

/* add type = TYPE_RECV_SOCKET_DATA full entry related interface */

inline struct cp_entry *get_full_entry_pos(struct cp_queue *q, unsigned int *index)
{
	unsigned int write_index;
	if (q->prev_half_entry_cache_used)
		if (add_cached_half_cp_entry_to_queue(q))
			return NULL;
	write_index = q->write_index;
	if (q->entries[write_index].status == STATUS_WAITING) {
		return NULL;
	}
	*index = write_index;
	return &q->entries[write_index];
}

inline void after_add_full_entry(struct cp_queue *q, unsigned int index, unsigned long start_addr, unsigned long entry_length)
{
	q->entries[index].interval_tree_node = add_interval_to_tree(q, start_addr, entry_length, index);
	// printk("after_add_full_entry entry info: from %ld to %ld ; interval info: from %ld to %ld, addr = %ld\n",
	//        (unsigned long)q->entries[index].to_va_base + q->entries[index].to_va_offset,
	//        (unsigned long)q->entries[index].to_va_base + q->entries[index].to_va_offset + q->entries[index].length - 1,
	//        q->entries[index].interval_tree_node->start, q->entries[index].interval_tree_node->last, (unsigned long)q->entries[index].interval_tree_node);
	q->write_index = (index + 1) % DEFUALT_CP_ENTRY_NUM;
}

/* add type = TYPE_SOCKET_RELEASE_SKB entry related interface */

inline struct cp_entry *get_release_entry_pos(struct cp_queue *q, unsigned int *index)
{
	unsigned int write_index;
	if (q->prev_half_entry_cache_used) {
		q->prev_release_entry_cache_used = true;
		return &(q->prev_release_entry_cache);
	}
	if (q->prev_release_entry_cache_used)
		printk("error in get_release_entry_pos!\n");
	write_index = q->write_index;
	if (q->entries[write_index].status == STATUS_WAITING) {
		return NULL;
	}
	*index = write_index;
	return &q->entries[write_index];
}

inline void after_add_release_entry(struct cp_queue *q, unsigned int index)
{
	q->write_index = (index + 1) % DEFUALT_CP_ENTRY_NUM;
}

/* flush queue cache related interface */

inline int flush_queue_cache(struct cp_queue *q)
{
	if (q->prev_half_entry_cache_used) {
		if (add_cached_half_cp_entry_to_queue(q))
			return -1;
		else
			return 0;
	}
	return 0;
}

inline int flush_queue_cache_and_add_log_entry(struct cp_queue *q, long io_length)
{
	// printk("flush queue cache\n");
	int write_index;
	struct cp_entry *entry;
	if (q->prev_half_entry_cache_used) {
		if (add_cached_half_cp_entry_to_queue(q))
			return -1;
		else
			goto ret;
	}
ret:
	write_index = q->write_index;
	entry = &q->entries[write_index];
	entry->type = TYPE_BREAKDOWN_LOG;
	entry->io_length = io_length;
	entry->status = STATUS_WAITING;
	q->write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;
	return 0;
}
/* old implement */

// inline int add_cached_half_cp_entry_to_queue(struct cp_queue *q, struct copyer_ctx *ctx)
// {
// 	{
// 		unsigned int write_index = q->write_index;
// 		if (q->entries[write_index].status == STATUS_WAITING)
// 			return -1;
// 		// printk("flush1 offset = %d entry to queue, index = %d, type = %d\n", q->prev_half_entry_cache->to_va_offset, write_index, q->prev_half_entry_cache->type);
// 		q->entries[write_index] = *(q->prev_half_entry_cache);
// 		q->entries[write_index].interval_tree_node =
// 			add_interval_to_tree(ctx, (unsigned long)q->prev_half_entry_cache->to_va_base + q->prev_half_entry_cache->to_va_offset,
// 					     q->prev_half_entry_cache->length, write_index);

// 		q->write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;

// 		// printk("to free prev_half_entry_cache\n");
// 		kfree(q->prev_half_entry_cache);
// 		q->prev_half_entry_cache = NULL;
// 	}

// 	if (q->prev_release_entry_cache) {
// 		unsigned int write_index = q->write_index;
// 		if (q->entries[write_index].status == STATUS_WAITING)
// 			return -1;
// 		// printk("flush2 release entry to queue, index = %d, status = %d\n", write_index, q->prev_release_entry_cache->status);
// 		q->entries[write_index] = *(q->prev_release_entry_cache);
// 		q->write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;

// 		// printk("to free prev_release_entry_cache\n");
// 		// kfree(q->prev_release_entry_cache);
// 		q->prev_release_entry_cache = NULL;
// 	}
// 	return 0;
// }

// // Notes: Currently only consider single thread. No race condition
// inline int add_half_cp_entry_to_queue(struct copyer_ctx *ctx, struct cp_entry *entry)
// {
// 	struct cp_queue *q = ctx->queue;
// 	{
// 		unsigned int write_index, write_index_next;
// 		if (!q->prev_half_entry_cache) {
// 			// printk("add offset = %d entry to cache, type = %d\n", entry->to_va_offset, entry->type);
// 			q->prev_half_entry_cache = entry;
// 			return 0;
// 		}
// 		write_index = q->write_index;
// 		write_index_next = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;
// 		if (q->entries[write_index].status == STATUS_WAITING || q->entries[write_index_next].status == STATUS_WAITING) {
// 			// spin_unlock(&q->locks[write_index]);
// 			return -1;
// 		}
// 		if (entry->to_va_base != q->prev_half_entry_cache->to_va_base ||
// 		    q->prev_half_entry_cache->to_va_offset + q->prev_half_entry_cache->length != entry->to_va_offset) {
// 			printk("error in add_half_cp_entry_to_queue!\n");
// 			return 0;
// 		}
// 		// printk("flush3 offset = %d entry to queue, offset = %d\n", q->prev_half_entry_cache->to_va_offset, write_index);
// 		q->entries[write_index] = *(q->prev_half_entry_cache);
// 		// printk("flush4 offset = %d entry to queue, offset = %d\n", entry->to_va_offset, write_index_next);
// 		q->entries[write_index_next] = *entry;
// 		q->entries[write_index].type = TYPE_RECV_SOCKET_DATA_TWO_BLOCK;

// 		q->entries[write_index].interval_tree_node =
// 			add_interval_to_tree(ctx, (unsigned long)q->prev_half_entry_cache->to_va_base + q->prev_half_entry_cache->to_va_offset,
// 					     q->prev_half_entry_cache->length + entry->length, write_index);

// 		q->write_index = (write_index_next + 1) % DEFUALT_CP_ENTRY_NUM;

// 		kfree(q->prev_half_entry_cache);
// 		kfree(entry);
// 		q->prev_half_entry_cache = NULL;
// 	}
// 	if (q->prev_release_entry_cache) {
// 		const unsigned int write_index = q->write_index;
// 		if (q->entries[write_index].status == STATUS_WAITING)
// 			return -1;
// 		q->entries[write_index] = *(q->prev_release_entry_cache);
// 		q->write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;

// 		kfree(q->prev_release_entry_cache);
// 		q->prev_release_entry_cache = NULL;
// 	}
// 	return 0;
// }
// EXPORT_SYMBOL(add_half_cp_entry_to_queue);

// inline int add_full_cp_entry_to_queue(struct copyer_ctx *ctx, struct cp_entry *entry)
// {
// 	struct cp_queue *q = ctx->queue;
// 	unsigned int write_index;
// 	if (entry->type == TYPE_RECV_SOCKET_DATA && q->prev_half_entry_cache)
// 		if (add_cached_half_cp_entry_to_queue(q, ctx))
// 			return -1;

// 	if (entry->type == TYPE_EMPTY && q->prev_half_entry_cache) {
// 		if (add_cached_half_cp_entry_to_queue(q, ctx))
// 			return -1;
// 		else
// 			return 0;
// 	}
// 	if (entry->type == TYPE_SOCKET_RELEASE_SKB && q->prev_half_entry_cache) {
// 		if (q->prev_release_entry_cache)
// 			printk("bug! q->prev_release_entry_cache != NULL\n");
// 		// printk("add prev_release_entry_cache entry to cache, status = %d\n", entry->to_va_offset, entry->status);
// 		q->prev_release_entry_cache = entry;
// 		return 0;
// 	}
// 	write_index = q->write_index;
// 	// if (ctx->should_wake_up)
// 	// 	wake_up(&ctx->sqo_wait);
// 	// spin_lock(&q->locks[write_index]);
// 	if (q->entries[write_index].status == STATUS_WAITING) {
// 		// spin_unlock(&q->locks[write_index]);
// 		return -1;
// 	}

// 	// printk("flush offset = %d entry to queue, offset = %d\n", entry->to_va_offset, write_index);
// 	q->entries[write_index] = *entry;

// 	if (entry->type == TYPE_RECV_SOCKET_DATA)
// 		q->entries[write_index].interval_tree_node =
// 			add_interval_to_tree(ctx, (unsigned long)entry->to_va_base + entry->to_va_offset, entry->length, write_index);

// 	// spin_unlock(&q->locks[write_index]);
// 	q->write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;
// 	if (entry->type == TYPE_SOCKET_RELEASE_SKB)
// 		kfree(entry);
// 	return 0;
// }
// EXPORT_SYMBOL(add_full_cp_entry_to_queue);
