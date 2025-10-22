#include <linux/errqueue.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/busy_poll.h>
#include <linux/syscalls.h>
#include <copyer/copyer.h>
// #include "cp_memlog.h"

extern int move_addr_to_user(struct sockaddr_storage *kaddr, int klen, void __user *uaddr, int __user *ulen);
extern struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed);
extern int tcp_peek_sndq(struct sock *sk, struct msghdr *msg, int len);
extern int tcp_recv_urg(struct sock *sk, struct msghdr *msg, int len, int flags);
extern int tcp_inq_hint(struct sock *sk);

// static inline int record_cp_entry(struct cp_queue *q, void *to_va, void *to_va_base, struct page *from_page, size_t page_offset, long len,
// 				  void __user *descriptor_buffer)
// {
// 	int entry_length, entry_offset = 0, basic_offset = (int)(to_va - to_va_base);
// 	struct cp_entry *submit_entry;
// 	int remaining_len = COPY_GRANULARITY - basic_offset % COPY_GRANULARITY;
// 	unsigned int index, prev_index;
// 	long to_va_offset;

// 	entry_length = (remaining_len < len) ? remaining_len : len;
// 	while (len > 0) {
// 		if (entry_length == COPY_GRANULARITY) {
// 			// printk("add full entry\n");
// 			// printk("53, from  = %ld\n", (unsigned long)to_va_base + to_va_offset);
// 			to_va_offset = basic_offset + entry_offset;
// 			submit_entry = get_full_entry_pos(q, &index);
// 			submit_entry->page = from_page;
// #ifdef MULTI_USER
// 			submit_entry->to_va_base_kernel = q->last_mapped_base_addr;
// #endif
// 			submit_entry->to_va_base = to_va_base;
// 			submit_entry->to_va_offset = to_va_offset;
// 			submit_entry->from_offset = page_offset + entry_offset;
// 			submit_entry->length = entry_length;
// 			submit_entry->type = TYPE_RECV_SOCKET_DATA;
// 			submit_entry->status = STATUS_WAITING;
// 			submit_entry->descriptor_buffer = descriptor_buffer;
// #ifdef MULTI_USER
// 			submit_entry->descriptor_buffer_kernel = q->last_mapped_descriptor_addr;
// #endif
// 			after_add_full_entry(q, index, (unsigned long)to_va_base + to_va_offset, entry_length);
// 		} else {
// 			// printk("66, from  = %ld\n", (unsigned long)to_va_base + basic_offset + entry_offset);
// 			// printk("call add half entry len = %ld\n", entry_length);
// 			submit_entry = get_half_entry_pos(q, &prev_index);
// 			submit_entry->page = from_page;
// 			submit_entry->to_va_base = to_va_base;
// #ifdef MULTI_USER
// 			submit_entry->to_va_base_kernel = q->last_mapped_base_addr;
// #endif
// 			submit_entry->to_va_offset = basic_offset + entry_offset;
// 			submit_entry->from_offset = page_offset + entry_offset;
// 			submit_entry->length = entry_length;
// 			submit_entry->type = TYPE_RECV_SOCKET_DATA;
// 			submit_entry->status = STATUS_WAITING;
// 			submit_entry->descriptor_buffer = descriptor_buffer;
// #ifdef MULTI_USER
// 			submit_entry->descriptor_buffer_kernel = q->last_mapped_descriptor_addr;
// #endif
// 			if (!q->prev_half_entry_cache_used) {
// 				// printk("call after_add_half_entry\n");
// 				after_add_half_entry(q, prev_index, entry_length);
// 			}
// 		}

// 		len -= entry_length;
// 		entry_offset += entry_length;
// 		entry_length = len > COPY_GRANULARITY ? COPY_GRANULARITY : len;
// 	}

// 	// struct cp_entry entry = {
// 	// 	.page = from_page,
// 	// 	.to_va_base = to_va_base,
// 	// 	.to_va_offset = (int)(to_va - to_va_base),
// 	// 	.from_offset = page_offset,
// 	// 	.length = len,
// 	// 	.type = TYPE_RECV_SOCKET_DATA,
// 	// 	.status = STATUS_WAITING,
// 	// };
// 	// submit_cp_entry(ctx, &entry);

// 	return 0;
// }

static inline int record_cp_entry2(struct cp_queue *q, void *to_va, void *to_va_base, struct page *from_page, size_t page_offset, long len,
				   void __user *descriptor_buffer)
{
	struct cp_entry *submit_entry = &q->entries[q->write_index];

	submit_entry->length = len;

	submit_entry->page = from_page;
	submit_entry->from_offset = page_offset;

	submit_entry->to_va = to_va;
#ifdef MULTI_USER
	submit_entry->to_va_base_kernel = q->last_mapped_base_addr;
#endif
	submit_entry->to_va_base = to_va_base;

	submit_entry->type = TYPE_RECV_SOCKET_DATA;
	submit_entry->status = STATUS_WAITING;
#ifdef MULTI_USER
	submit_entry->descriptor_buffer_kernel = q->last_mapped_descriptor_addr;
#else
	submit_entry->descriptor_buffer = descriptor_buffer;
#endif
	q->write_index = (q->write_index + 1) % DEFUALT_CP_ENTRY_NUM;

	return 0;
}

static inline size_t COPYER_DRYRUN_WRAP(simple_record_iter)(struct cp_queue *q, struct page *page, size_t page_offset, size_t bytes,
							    struct iov_iter *i, void __user *descriptor_buffer, void __user *buf_base)
{
	/* adapted from simple_copy_to_iter */
	// if (unlikely(!check_copy_size(addr, bytes, true)))
	// 	return 0;
#define iterate_and_advance(i, n, base, len, off, I)                                                                                                 \
	{                                                                                                                                            \
		if (unlikely(i->count < n))                                                                                                          \
			n = i->count;                                                                                                                \
		if (likely(n)) {                                                                                                                     \
			const struct iovec *iov = i->iov;                                                                                            \
			void __user *base;                                                                                                           \
			size_t len;                                                                                                                  \
			iterate_iovec(i, n, base, len, off, iov, (I)) i->nr_segs -= iov - i->iov;                                                    \
			i->iov = iov;                                                                                                                \
			i->count -= n;                                                                                                               \
		}                                                                                                                                    \
	}

#define iterate_iovec(i, n, base, len, off, __p, STEP)                                                                                               \
	{                                                                                                                                            \
		size_t off = 0;                                                                                                                      \
		size_t skip = i->iov_offset;                                                                                                         \
		do {                                                                                                                                 \
			len = min(n, __p->iov_len - skip);                                                                                           \
			if (likely(len)) {                                                                                                           \
				base = __p->iov_base + skip;                                                                                         \
				len -= (STEP);                                                                                                       \
				off += len;                                                                                                          \
				skip += len;                                                                                                         \
				n -= len;                                                                                                            \
				if (skip < __p->iov_len)                                                                                             \
					break;                                                                                                       \
			}                                                                                                                            \
			__p++;                                                                                                                       \
			skip = 0;                                                                                                                    \
		} while (n);                                                                                                                         \
		i->iov_offset = skip;                                                                                                                \
		n = off;                                                                                                                             \
	}
	iterate_and_advance(i, bytes, base, len, off,
			    record_cp_entry2(q, base, buf_base, page, page_offset + off, len, descriptor_buffer)) return bytes;
}

static int COPYER_DRYRUN_WRAP(__skb_datagram_iter)(struct cp_queue *q, const struct sk_buff *skb, int offset, struct iov_iter *to, int len,
						   bool fault_short, void __user *descriptor_buffer, void __user *buf_base)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset, start_off = offset, n;
	struct sk_buff *frag_iter;

	/* Copy header. */
	/* Do not use async to copy header */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		n = copy_to_iter(skb->data + offset, copy, to);
		offset += n;
		if (n != copy)
			goto short_copy;
		if ((len -= copy) == 0)
			return 0;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			struct page *page = skb_frag_page(frag);
			if (copy > len)
				copy = len;
			n = simple_record_iter_copyer_dryrun(q, page, skb_frag_off(frag) + offset - start, copy, to, descriptor_buffer, buf_base);
			offset += n;
			if (n != copy)
				goto short_copy;
			if (!(len -= copy))
				return 0;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter)
	{
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (__skb_datagram_iter_copyer_dryrun(q, frag_iter, offset - start, to, copy, fault_short, descriptor_buffer, buf_base))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

	/* This is not really a user copy fault, but rather someone
	 * gave us a bogus length on the skb.  We should probably
	 * print a warning here as it may indicate a kernel bug.
	 */

fault:
	iov_iter_revert(to, offset - start_off);
	return -EFAULT;

short_copy:
#ifdef HJK_DEBUG
	printk("short_copy!\n");
#endif
	if (fault_short || iov_iter_count(to))
		goto fault;

	return 0;
}

/**
 *	skb_copy_datagram_iter - Copy a datagram to an iovec iterator.
 *	@skb: buffer to copy
 *	@offset: offset in the buffer to start copying from
 *	@to: iovec iterator to copy to
 *	@len: amount of data to copy from buffer to iovec
 */
int COPYER_DRYRUN_WRAP(skb_copy_datagram_iter)(struct cp_queue *q, const struct sk_buff *skb, int offset, struct iov_iter *to, int len,
					       void __user *descriptor_buffer, void __user *buf_base)
{
	return __skb_datagram_iter_copyer_dryrun(q, skb, offset, to, len, false, descriptor_buffer, buf_base);
}

static inline int COPYER_WRAP(skb_copy_datagram_msg)(struct cp_queue *q, const struct sk_buff *from, int offset, struct msghdr *msg, int size,
						     void __user *descriptor_buffer, void __user *buf_base)
{
	// #ifdef HJK_DEBUG
	// 	printk("skb_copy_datagram_msg, size = %ld\n", size);
	// #endif
	// if (size >= ASYNC_THRESHOLD)
	// 	/* async copy */
	return skb_copy_datagram_iter_copyer_dryrun(q, from, offset, &msg->msg_iter, size, descriptor_buffer, buf_base);
	// else
	/* sync copy */
	// return skb_copy_datagram_iter(from, offset, &msg->msg_iter, size);
}

/*
 *	This routine copies from a sock struct into the user buffer.
 *
 *	Technical note: in 2.3 we work on _locked_ socket, so that
 *	tricks with *seq access order and skb->users are not required.
 *	Probably, code can be easily improved even more.
 */

static int COPYER_WRAP(tcp_recvmsg_locked)(struct cp_queue *q, struct sock *sk, struct msghdr *msg, size_t len, int nonblock, int flags,
					   struct scm_timestamping_internal *tss, int *cmsg_flags, void __user *descriptor_buffer,
					   void __user *buf_base)
{
	struct tcp_sock *tp = tcp_sk(sk);
	int copied = 0;
	u32 peek_seq;
	u32 *seq;
	unsigned long used;
	int err;
	int target; /* Read at least this many bytes */
	long timeo;
	struct sk_buff *skb, *last;
	u32 urg_hole = 0;

	struct page **pages_user;
	struct page **pages_user_descriptor;

	unsigned int index;
	struct cp_entry *release_entry;

	err = -ENOTCONN;
	if (sk->sk_state == TCP_LISTEN)
		goto out;

	if (tp->recvmsg_inq)
		*cmsg_flags = TCP_CMSG_INQ;
	timeo = sock_rcvtimeo(sk, nonblock);

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		goto recv_urg;

#ifdef MULTI_USER

#define MAX_MAP_PAGES (1024 * 1024 / (PAGE_SIZE) + 1)

	if (q->last_used_desriptor != (unsigned long)descriptor_buffer) {
		q->last_used_desriptor = (unsigned long)descriptor_buffer;
		pages_user_descriptor = kmalloc_array(1, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)descriptor_buffer, 1, FOLL_WRITE, pages_user_descriptor);
		q->last_mapped_descriptor_addr = vmap(pages_user_descriptor, 1, VM_MAP, PAGE_KERNEL) + (unsigned long)descriptor_buffer % (PAGE_SIZE);
	}
	if (q->last_used_base != (unsigned long)buf_base) {
		q->last_used_base = (unsigned long)buf_base;
		pages_user = kmalloc_array(MAX_MAP_PAGES, sizeof(struct page *), GFP_KERNEL);
		get_user_pages_fast((unsigned long)buf_base, MAX_MAP_PAGES, FOLL_WRITE, pages_user);
		q->last_mapped_base_addr = vmap(pages_user, MAX_MAP_PAGES, VM_MAP, PAGE_KERNEL) + (unsigned long)buf_base % (PAGE_SIZE);
	}

#endif

	if (unlikely(tp->repair)) {
		err = -EPERM;
		if (!(flags & MSG_PEEK))
			goto out;

		if (tp->repair_queue == TCP_SEND_QUEUE)
			goto recv_sndq;

		err = -EINVAL;
		if (tp->repair_queue == TCP_NO_QUEUE)
			goto out;

		/* 'common' recv queue MSG_PEEK-ing */
	}

	seq = &tp->copied_seq;
	if (flags & MSG_PEEK) {
		peek_seq = tp->copied_seq;
		seq = &peek_seq;
	}

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	do {
		u32 offset;

		/* Are we at urgent data? Stop if we have read anything or have SIGURG pending. */
		if (tp->urg_data && tp->urg_seq == *seq) {
			if (copied)
				break;
			if (signal_pending(current)) {
				copied = timeo ? sock_intr_errno(timeo) : -EAGAIN;
				break;
			}
		}

		/* Next get a buffer. */

		last = skb_peek_tail(&sk->sk_receive_queue);
		skb_queue_walk(&sk->sk_receive_queue, skb)
		{
			last = skb;
			/* Now that we have two receive queues this
			 * shouldn't happen.
			 */
			if (WARN(before(*seq, TCP_SKB_CB(skb)->seq), "TCP recvmsg seq # bug: copied %X, seq %X, rcvnxt %X, fl %X\n", *seq,
				 TCP_SKB_CB(skb)->seq, tp->rcv_nxt, flags))
				break;

			offset = *seq - TCP_SKB_CB(skb)->seq;
			if (unlikely(TCP_SKB_CB(skb)->tcp_flags & TCPHDR_SYN)) {
				pr_err_once("%s: found a SYN, please report !\n", __func__);
				offset--;
			}
			if (offset < skb->len)
				goto found_ok_skb;
			if (TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN)
				goto found_fin_ok;
			WARN(!(flags & MSG_PEEK), "TCP recvmsg seq # bug 2: copied %X, seq %X, rcvnxt %X, fl %X\n", *seq, TCP_SKB_CB(skb)->seq,
			     tp->rcv_nxt, flags);
		}

		/* Well, if we have backlog, try to process it now yet. */

		if (copied >= target && !READ_ONCE(sk->sk_backlog.tail))
			break;

		if (copied) {
			if (sk->sk_err || sk->sk_state == TCP_CLOSE || (sk->sk_shutdown & RCV_SHUTDOWN) || !timeo || signal_pending(current))
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;

			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}

			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			if (sk->sk_state == TCP_CLOSE) {
				/* This occurs when user tries to read
				 * from never connected socket.
				 */
				copied = -ENOTCONN;
				break;
			}

			if (!timeo) {
				copied = -EAGAIN;
				break;
			}

			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}

		tcp_cleanup_rbuf(sk, copied);

		if (copied >= target) {
			/* Do not sleep, just process backlog. */
			release_sock(sk);
			lock_sock(sk);
		} else {
			sk_wait_data(sk, &timeo, last);
		}

		if ((flags & MSG_PEEK) && (peek_seq - copied - urg_hole != tp->copied_seq)) {
			net_dbg_ratelimited("TCP(%s:%d): Application bug, race in MSG_PEEK\n", current->comm, task_pid_nr(current));
			peek_seq = tp->copied_seq;
		}
		continue;

	found_ok_skb:
		/* Ok so how much can we use? */
		used = skb->len - offset;
		if (len < used)
			used = len;

		/* Do we have urgent data here? */
		if (tp->urg_data) {
			u32 urg_offset = tp->urg_seq - *seq;
			if (urg_offset < used) {
				if (!urg_offset) {
					if (!sock_flag(sk, SOCK_URGINLINE)) {
						WRITE_ONCE(*seq, *seq + 1);
						urg_hole++;
						offset++;
						used--;
						if (!used)
							goto skip_copy;
					}
				} else
					used = urg_offset;
			}
		}

		if (!(flags & MSG_TRUNC)) {
			err = skb_copy_datagram_msg_copyer(q, skb, offset, msg, used, descriptor_buffer, buf_base);
			if (err) {
				/* Exception. Bailout! */
				if (!copied)
					copied = -EFAULT;
				break;
			}
		}

		WRITE_ONCE(*seq, *seq + used);
		copied += used;
		len -= used;

		tcp_rcv_space_adjust(sk);

	skip_copy:
		if (tp->urg_data && after(tp->copied_seq, tp->urg_seq)) {
			tp->urg_data = 0;
			tcp_fast_path_check(sk);
		}

		if (TCP_SKB_CB(skb)->has_rxtstamp) {
			tcp_update_recv_tstamps(skb, tss);
			*cmsg_flags |= TCP_CMSG_TS;
		}

		if (used + offset < skb->len)
			continue;

		if (TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN)
			goto found_fin_ok;
		/* edited by copyer */
		if (!(flags & MSG_PEEK)) {
			// sk_eat_skb(sk, skb);
			__skb_unlink(skb, &sk->sk_receive_queue);
			// release_entry = get_release_entry_pos(q, &index);
			release_entry = &q->entries[q->write_index];
			release_entry->skb = (struct sk_buff *)skb;
			release_entry->type = TYPE_SOCKET_RELEASE_SKB;
			release_entry->status = STATUS_WAITING;
			q->write_index = (q->write_index + 1) % DEFUALT_CP_ENTRY_NUM;
			// if (!q->prev_release_entry_cache_used)
			// 	after_add_release_entry(q, index);
		}
		/* edited by copyer */
		continue;

	found_fin_ok:
		// #ifdef HJK_DEBUG
		// 		printk("found_fin_ok\n");
		// #endif
		/* Process the FIN. */
		WRITE_ONCE(*seq, *seq + 1);
		if (!(flags & MSG_PEEK)) {
			// sk_eat_skb(sk, skb);
			__skb_unlink(skb, &sk->sk_receive_queue);
			// release_entry = get_release_entry_pos(q, &index);
			release_entry = &q->entries[q->write_index];
			release_entry->skb = (struct sk_buff *)skb;
			release_entry->type = TYPE_SOCKET_RELEASE_SKB;
			release_entry->status = STATUS_WAITING;
			q->write_index = (q->write_index + 1) % DEFUALT_CP_ENTRY_NUM;
			// if (!q->prev_release_entry_cache_used)
			// 	after_add_release_entry(q, index);
		}

		break;
	} while (len > 0);

	/* According to UNIX98, msg_name/msg_namelen are ignored
	 * on connected socket. I was just happy when found this 8) --ANK
	 */

	/* Clean up data we have read: This will do ACK frames. */
	tcp_cleanup_rbuf(sk, copied);
	return copied;

out:
	return err;

recv_urg:
	err = tcp_recv_urg(sk, msg, len, flags);
	goto out;

recv_sndq:
	err = tcp_peek_sndq(sk, msg, len);
	goto out;
}

int COPYER_WRAP(tcp_recvmsg)(int fd_queue, struct sock *sk, struct msghdr *msg, size_t len, int nonblock, int flags, int *addr_len,
			     void __user *descriptor_buffer, void __user *buf_base)
{
	// #ifdef HJK_DEBUG
	// 	printk("tcp_recvmsg\n");
	// #endif
	int cmsg_flags = 0, ret, inq;
	struct scm_timestamping_internal tss;

	struct cp_queue *q;
	struct file *file = fget(fd_queue);

	if (!file) {
		printk("[copyer] NO SUCH FILE");
		return -EFAULT;
	}
	q = &((struct copyer_ctx *)(file->private_data))->queues_for_recv->queue;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	if (sk_can_busy_loop(sk) && skb_queue_empty_lockless(&sk->sk_receive_queue) && sk->sk_state == TCP_ESTABLISHED)
		sk_busy_loop(sk, nonblock);

	lock_sock(sk);
	ret = tcp_recvmsg_locked_copyer(q, sk, msg, len, nonblock, flags, &tss, &cmsg_flags, descriptor_buffer, buf_base);

	release_sock(sk);

	if (cmsg_flags && ret >= 0) {
		if (cmsg_flags & TCP_CMSG_TS)
			tcp_recv_timestamp(msg, sk, &tss);
		if (cmsg_flags & TCP_CMSG_INQ) {
			inq = tcp_inq_hint(sk);
			put_cmsg(msg, SOL_TCP, TCP_CM_INQ, sizeof(inq), &inq);
		}
	}
	return ret;
}

int COPYER_WRAP(udp_recvmsg)(int fd_queue, struct sock *sk, struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len)
{
	printk("udp_recvmsg\n");
	return 0;
}

int COPYER_WRAP(inet_recvmsg)(struct socket *sock, struct msghdr *msg, size_t size, int flags, void __user *descriptor_buffer, void __user *buf_base)
{
	// #ifdef HJK_DEBUG
	// 	printk("inet_recvmsg size = %ld\n", size);
	// #endif
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	if (likely(!(flags & MSG_ERRQUEUE)))
		sock_rps_record_flow(sk);

	err = likely(sk->sk_prot->recvmsg == tcp_recvmsg) ?
		      tcp_recvmsg_copyer(sock->file->queue_fd_out, sk, msg, size, flags & MSG_DONTWAIT, flags & ~MSG_DONTWAIT, &addr_len,
					 descriptor_buffer, buf_base) :
		      udp_recvmsg_copyer(sock->file->queue_fd_out, sk, msg, size, flags & MSG_DONTWAIT, flags & ~MSG_DONTWAIT, &addr_len);

	// err = INDIRECT_CALL_2(sk->sk_prot->recvmsg, tcp_recvmsg_copyer, udp_recvmsg_copyer,
	// 		      sk, msg, size, flags & MSG_DONTWAIT,
	// 		      flags & ~MSG_DONTWAIT, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}

/**
 *	sock_recvmsg - receive a message from @sock
 *	@sock: socket
 *	@msg: message to receive
 *	@flags: message flags
 *
 *	Receives @msg from @sock, passing through LSM. Returns the total number
 *	of bytes received, or an error.
 */
int COPYER_WRAP(sock_recvmsg)(struct socket *sock, struct msghdr *msg, int flags, void __user *descriptor_buffer, void __user *buf_base)
{
	// int err = security_socket_recvmsg(sock, msg, msg_data_left(msg), flags);
	// return err ?: sock_recvmsg_nosec(sock, msg, flags);
	return inet_recvmsg_copyer(sock, msg, msg_data_left(msg), flags, descriptor_buffer, buf_base);
}
EXPORT_SYMBOL(sock_recvmsg_copyer);

/*
 *	Receive a frame from the socket and optionally record the address of the
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */
int COPYER_WRAP(__sys_recvfrom)(int fd, void __user *ubuf, void __user *buf_base, size_t size, unsigned int flags, void __user *descriptor_buffer)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	// struct sockaddr_storage address;
	int err;
	// int err2;
	int fput_needed;

	// #ifdef HJK_DEBUG
	// 	printk("__sys_recvfrom size = %ld, usr_va = %ld\n", size, (long)ubuf);
	// #endif
	err = import_single_range(READ, ubuf, size, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	/* Save some cycles and don't copy the address if not needed */
	// msg.msg_name = addr ? (struct sockaddr *)&address : NULL;
	msg.msg_name = NULL;
	/* We assume all kernel code knows the size of sockaddr_storage */
	msg.msg_namelen = 0;
	msg.msg_iocb = NULL;
	msg.msg_flags = 0;
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg_copyer(sock, &msg, flags, descriptor_buffer, buf_base);

	// if (err >= 0 && addr != NULL) {
	// 	err2 = move_addr_to_user(&address, msg.msg_namelen, addr, addr_len);
	// 	if (err2 < 0)
	// 		err = err2;
	// }

	fput_light(sock->file, fput_needed);
out:
	return err;
}

// SYSCALL_DEFINE6(recvfrom_copyer, int, fd, void __user *, ubuf, size_t, size, unsigned int, flags, struct sockaddr __user *, addr, int __user *,
// 		addr_len)
SYSCALL_DEFINE6(recvfrom_copyer, int, fd, void __user *, ubuf, void *__user, buf_base, size_t, size, unsigned int, flags, void __user *,
		descriptor_buffer)
{
	// #ifdef HJK_DEBUG
	// 	printk("this is recvfrom_copyer\n");
	// #endif
	// int fd = (int)fds;
	// int fd_queue = (int)(fds >> 32);
	return __sys_recvfrom_copyer(fd, ubuf, buf_base, size, flags, descriptor_buffer);
}
