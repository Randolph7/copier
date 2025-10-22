#include <linux/errqueue.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/busy_poll.h>
#include <linux/syscalls.h>
#include <copyer/copyer.h>

extern int move_addr_to_user(struct sockaddr_storage *kaddr, int klen, void __user *uaddr, int __user *ulen);
extern struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed);
extern int tcp_peek_sndq(struct sock *sk, struct msghdr *msg, int len);
extern int tcp_recv_urg(struct sock *sk, struct msghdr *msg, int len, int flags);
extern int tcp_inq_hint(struct sock *sk);

struct cp_queue lazy_cp_queue;

static inline int record_cp_entry_lazy(void *to_va, struct page *from_page, size_t page_offset, long len, int *index)
{
	unsigned int write_index;
	struct cp_entry *submit_entry;

	spin_lock(&lazy_cp_queue.lock);
	write_index = lazy_cp_queue.write_index;
	submit_entry = &lazy_cp_queue.entries[write_index];

	submit_entry->length = len;
	submit_entry->page = from_page;
	submit_entry->from_offset = page_offset;
	submit_entry->to_va = to_va;
	submit_entry->type = TYPE_RECV_SOCKET_DATA;
	submit_entry->status = STATUS_WAITING;
	submit_entry->skb = NULL;

	lazy_cp_queue.write_index = (write_index + 1) % DEFUALT_CP_ENTRY_NUM;

	spin_unlock(&lazy_cp_queue.lock);

	// printk("add %lu index %d\n", len, write_index);

	*index = write_index;

	return 0;
}

static inline size_t simple_record_iter_lazy(struct page *page, size_t page_offset, size_t bytes, struct iov_iter *i, int *index)
{
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
	iterate_and_advance(i, bytes, base, len, off, record_cp_entry_lazy(base, page, page_offset + off, len, index)) return bytes;
}

static int __skb_datagram_iter_lazy(const struct sk_buff *skb, int offset, struct iov_iter *to, int len, bool fault_short, int *index)
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
			n = simple_record_iter_lazy(page, skb_frag_off(frag) + offset - start, copy, to, index);
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
			if (__skb_datagram_iter_lazy(frag_iter, offset - start, to, copy, fault_short, index))
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

int skb_copy_datagram_iter_lazy(const struct sk_buff *skb, int offset, struct iov_iter *to, int len, int *index)
{
	return __skb_datagram_iter_lazy(skb, offset, to, len, false, index);
}

static inline int skb_copy_datagram_msg_lazy(const struct sk_buff *from, int offset, struct msghdr *msg, int size, int *index)
{
	return skb_copy_datagram_iter_lazy(from, offset, &msg->msg_iter, size, index);
}

static int tcp_recvmsg_locked_lazy(struct sock *sk, struct msghdr *msg, size_t len, int nonblock, int flags, struct scm_timestamping_internal *tss,
				   int *cmsg_flags)
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

	int index = -1;

	err = -ENOTCONN;
	if (sk->sk_state == TCP_LISTEN)
		goto out;

	if (tp->recvmsg_inq)
		*cmsg_flags = TCP_CMSG_INQ;
	timeo = sock_rcvtimeo(sk, nonblock);

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		goto recv_urg;

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
			err = skb_copy_datagram_msg_lazy(skb, offset, msg, used, &index);
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

		if (!(flags & MSG_PEEK)) {
			// sk_eat_skb(sk, skb);
			__skb_unlink(skb, &sk->sk_receive_queue);
			// release_entry = get_release_entry_pos(q, &index);
			if (index >= 0) {
				lazy_cp_queue.entries[index].skb = skb;
			} else {
				__kfree_skb(skb);
			}
			// if (!q->prev_release_entry_cache_used)
			// 	after_add_release_entry(q, index);
		}
		continue;

	found_fin_ok:
		/* Process the FIN. */
		WRITE_ONCE(*seq, *seq + 1);
		if (!(flags & MSG_PEEK)) {
			// sk_eat_skb(sk, skb);
			__skb_unlink(skb, &sk->sk_receive_queue);
			// release_entry = get_release_entry_pos(q, &index);
			if (index >= 0) {
				lazy_cp_queue.entries[index].skb = skb;
			} else {
				__kfree_skb(skb);
			}
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

int tcp_recvmsg_lazy(struct sock *sk, struct msghdr *msg, size_t len, int nonblock, int flags, int *addr_len)
{
	int cmsg_flags = 0, ret, inq;
	struct scm_timestamping_internal tss;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	if (sk_can_busy_loop(sk) && skb_queue_empty_lockless(&sk->sk_receive_queue) && sk->sk_state == TCP_ESTABLISHED)
		sk_busy_loop(sk, nonblock);

	lock_sock(sk);
	ret = tcp_recvmsg_locked_lazy(sk, msg, len, nonblock, flags, &tss, &cmsg_flags);
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

int udp_recvmsg_lazy(struct sock *sk, struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len)
{
	printk("udp_recvmsg\n");
	return 0;
}

int inet_recvmsg_lazy(struct socket *sock, struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	if (likely(!(flags & MSG_ERRQUEUE)))
		sock_rps_record_flow(sk);

	err = likely(sk->sk_prot->recvmsg == tcp_recvmsg) ? tcp_recvmsg_lazy(sk, msg, size, flags & MSG_DONTWAIT, flags & ~MSG_DONTWAIT, &addr_len) :
							    udp_recvmsg_lazy(sk, msg, size, flags & MSG_DONTWAIT, flags & ~MSG_DONTWAIT, &addr_len);

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
int sock_recvmsg_lazy(struct socket *sock, struct msghdr *msg, int flags)
{
	// int err = security_socket_recvmsg(sock, msg, msg_data_left(msg), flags);
	// return err ?: sock_recvmsg_nosec(sock, msg, flags);
	return inet_recvmsg_lazy(sock, msg, msg_data_left(msg), flags);
}

/*
 *	Receive a frame from the socket and optionally record the address of the
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */
int __sys_recvfrom_lazy(int fd, void __user *ubuf, size_t size, unsigned int flags, struct sockaddr __user *addr, int __user *addr_len)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_storage address;
	int err, err2;
	int fput_needed;

	err = import_single_range(READ, ubuf, size, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	/* Save some cycles and don't copy the address if not needed */
	msg.msg_name = addr ? (struct sockaddr *)&address : NULL;
	/* We assume all kernel code knows the size of sockaddr_storage */
	msg.msg_namelen = 0;
	msg.msg_iocb = NULL;
	msg.msg_flags = 0;
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg_lazy(sock, &msg, flags);

	if (err >= 0 && addr != NULL) {
		err2 = move_addr_to_user(&address, msg.msg_namelen, addr, addr_len);
		if (err2 < 0)
			err = err2;
	}

	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE6(recvfrom_lazy, int, fd, void __user *, ubuf, size_t, size, unsigned int, flags, struct sockaddr __user *, addr, int __user *,
		addr_len)
{
	return __sys_recvfrom_lazy(fd, ubuf, size, flags, addr, addr_len);
}

SYSCALL_DEFINE0(recvfrom_lazy_init)
{
	int i;
	memset(&lazy_cp_queue, 0, sizeof(struct cp_queue));
	for (i = 0; i < DEFUALT_CP_ENTRY_NUM; i++) {
		lazy_cp_queue.entries[i].status = STATUS_DONE;
	}
	spin_lock_init(&lazy_cp_queue.lock);
	return 0;
}
