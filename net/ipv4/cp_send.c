#include "linux/compiler.h"
#include "linux/slab.h"
#include <linux/syscalls.h>
#include <net/icmp.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <copyer/copier.h>
#include <linux/netfilter.h>
#include <net/pkt_sched.h>

#define COPIER_WAIT(skb)                                                                                                                             \
	{                                                                                                                                            \
		if (unlikely(*(skb->descriptor) != 1)) {                                                                                             \
			while (*(skb->descriptor) != 1)                                                                                              \
				;                                                                                                                    \
		}                                                                                                                                    \
	}

// #define COPIER_WAIT(skb) {}

extern struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed);
extern inline bool forced_push(const struct tcp_sock *tp);
extern void tcp_tx_timestamp(struct sock *sk, u16 tsflags);
extern inline void tcp_mark_urg(struct tcp_sock *tp, int flags);
extern bool tcp_should_autocork(struct sock *sk, struct sk_buff *skb, int size_goal);
extern int tcp_mtu_probe(struct sock *sk);
extern u32 tcp_tso_segs(struct sock *sk, unsigned int mss_now);
extern int tcp_init_tso_segs(struct sk_buff *skb, unsigned int mss_now);
extern bool tcp_pacing_check(struct sock *sk);
extern inline unsigned int tcp_cwnd_test(const struct tcp_sock *tp, const struct sk_buff *skb);
extern bool tcp_snd_wnd_test(const struct tcp_sock *tp, const struct sk_buff *skb, unsigned int cur_mss);
extern inline bool tcp_nagle_test(const struct tcp_sock *tp, const struct sk_buff *skb, unsigned int cur_mss, int nonagle);
extern bool tcp_tso_should_defer(struct sock *sk, struct sk_buff *skb, bool *is_cwnd_limited, bool *is_rwnd_limited, u32 max_segs);
extern inline bool tcp_urg_mode(const struct tcp_sock *tp);
extern unsigned int tcp_mss_split_point(const struct sock *sk, const struct sk_buff *skb, unsigned int mss_now, unsigned int max_segs, int nonagle);
extern bool tcp_small_queue_check(struct sock *sk, const struct sk_buff *skb, unsigned int factor);
extern void tcp_event_new_data_sent(struct sock *sk, struct sk_buff *skb);
extern void tcp_minshall_update(struct tcp_sock *tp, unsigned int mss_now, const struct sk_buff *skb);
extern int tso_fragment(struct sock *sk, struct sk_buff *skb, unsigned int len, unsigned int mss_now, gfp_t gfp);
extern void tcp_cwnd_validate(struct sock *sk, bool is_cwnd_limited);
extern u16 tcp_select_window(struct sock *sk);
extern void tcp_update_skb_after_send(struct sock *sk, struct sk_buff *skb, u64 prior_wstamp);
extern unsigned int tcp_established_options(struct sock *sk, struct sk_buff *skb, struct tcp_out_options *opts, struct tcp_md5sig_key **md5);
extern void tcp_options_write(__be32 *ptr, struct tcp_sock *tp, struct tcp_out_options *opts);
extern unsigned int tcp_syn_options(struct sock *sk, struct sk_buff *skb, struct tcp_out_options *opts, struct tcp_md5sig_key **md5);
extern inline void tcp_event_ack_sent(struct sock *sk, unsigned int pkts, u32 rcv_nxt);
extern void tcp_ecn_send(struct sock *sk, struct sk_buff *skb, struct tcphdr *th, int tcp_header_len);
extern void tcp_event_data_sent(struct tcp_sock *tp, struct sock *sk);
extern inline int ip_select_ttl(struct inet_sock *inet, struct dst_entry *dst);
extern void ip_copy_addrs(struct iphdr *iph, const struct flowi4 *fl4);
extern void ip_frag_ipcb(struct sk_buff *from, struct sk_buff *to, bool first_frag);
extern void skb_update_prio(struct sk_buff *skb);
extern void qdisc_pkt_len_init(struct sk_buff *skb);
extern struct sk_buff *validate_xmit_skb(struct sk_buff *skb, struct net_device *dev, bool *again);
extern void __qdisc_run(struct Qdisc *q);
/*
	TODO: deeper
*/
extern int dev_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *q, struct sk_buff **to_free, struct netdev_queue *txq);
extern bool sch_direct_xmit(struct sk_buff *skb, struct Qdisc *q, struct net_device *dev, struct netdev_queue *txq, spinlock_t *root_lock,
			    bool validate);
extern int xmit_one(struct sk_buff *skb, struct net_device *dev, struct netdev_queue *txq, bool more);

// #define COPIER_WAIT(skb)                                                                                                                             \
// 	while (global_descriptor != COPIER_BLOCK_DONE)                                                                                               \
// 		;

static inline int COPYER_WRAP(__dev_xmit_skb)(struct sk_buff *skb, struct Qdisc *q, struct net_device *dev, struct netdev_queue *txq)
{
	spinlock_t *root_lock = qdisc_lock(q);
	struct sk_buff *to_free = NULL;
	bool contended;
	int rc;

	qdisc_calculate_pkt_len(skb, q);

	COPIER_WAIT(skb)

	if (q->flags & TCQ_F_NOLOCK) {
		if (q->flags & TCQ_F_CAN_BYPASS && nolock_qdisc_is_empty(q) && qdisc_run_begin(q)) {
			/* Retest nolock_qdisc_is_empty() within the protection
			 * of q->seqlock to protect from racing with requeuing.
			 */
			if (unlikely(!nolock_qdisc_is_empty(q))) {
				rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
				__qdisc_run(q);
				qdisc_run_end(q);

				goto no_lock_out;
			}

			qdisc_bstats_cpu_update(q, skb);
			if (sch_direct_xmit(skb, q, dev, txq, NULL, true) && !nolock_qdisc_is_empty(q))
				__qdisc_run(q);

			qdisc_run_end(q);
			return NET_XMIT_SUCCESS;
		}

		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		qdisc_run(q);

	no_lock_out:
		if (unlikely(to_free))
			kfree_skb_list(to_free);
		return rc;
	}

	/*
	 * Heuristic to force contended enqueues to serialize on a
	 * separate lock before trying to get qdisc main lock.
	 * This permits qdisc->running owner to get the lock more
	 * often and dequeue packets faster.
	 */
	contended = qdisc_is_running(q);
	if (unlikely(contended))
		spin_lock(&q->busylock);

	spin_lock(root_lock);
	if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
		__qdisc_drop(skb, &to_free);
		rc = NET_XMIT_DROP;
	} else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) && qdisc_run_begin(q)) {
		/*
		 * This is a work-conserving queue; there are no old skbs
		 * waiting to be sent out; and the qdisc is not running -
		 * xmit the skb directly.
		 */

		qdisc_bstats_update(q, skb);

		if (sch_direct_xmit(skb, q, dev, txq, root_lock, true)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
		}

		qdisc_run_end(q);
		rc = NET_XMIT_SUCCESS;
	} else {
		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		if (qdisc_run_begin(q)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
			qdisc_run_end(q);
		}
	}
	spin_unlock(root_lock);
	if (unlikely(to_free))
		kfree_skb_list(to_free);
	if (unlikely(contended))
		spin_unlock(&q->busylock);
	return rc;
}

struct sk_buff *COPYER_WRAP(dev_hard_start_xmit)(struct sk_buff *first, struct net_device *dev, struct netdev_queue *txq, int *ret)
{
	struct sk_buff *skb = first;
	int rc = NETDEV_TX_OK;

	while (skb) {
		struct sk_buff *next = skb->next;

		skb_mark_not_on_list(skb);
		// printk("start waiting\n");
		COPIER_WAIT(skb);
		// printk("end waiting\n");
		// printk("before xmit_one\n");
		rc = xmit_one(skb, dev, txq, next != NULL);
		if (unlikely(!dev_xmit_complete(rc))) {
			skb->next = next;
			goto out;
		}

		skb = next;
		if (netif_tx_queue_stopped(txq) && skb) {
			rc = NETDEV_TX_BUSY;
			break;
		}
	}

out:
	*ret = rc;
	return skb;
}

/**
 *	__dev_queue_xmit - transmit a buffer
 *	@skb: buffer to transmit
 *	@sb_dev: suboordinate device used for L2 forwarding offload
 *
 *	Queue a buffer for transmission to a network device. The caller must
 *	have set the device and priority and built the buffer before calling
 *	this function. The function can be called from an interrupt.
 *
 *	A negative errno code is returned on a failure. A success does not
 *	guarantee the frame will be transmitted as it may be dropped due
 *	to congestion or traffic shaping.
 *
 * -----------------------------------------------------------------------------------
 *      I notice this method can also return errors from the queue disciplines,
 *      including NET_XMIT_DROP, which is a positive value.  So, errors can also
 *      be positive.
 *
 *      Regardless of the return value, the skb is consumed, so it is currently
 *      difficult to retry a send to this method.  (You can bump the ref count
 *      before sending to hold a reference for retry if you are careful.)
 *
 *      When calling this method, interrupts MUST be enabled.  This is because
 *      the BH enable code must have IRQs enabled so that it will not deadlock.
 *          --BLG
 */
static int COPYER_WRAP(__dev_queue_xmit)(struct sk_buff *skb, struct net_device *sb_dev)
{
	// printk("__dev_queue_xmit skb = %llu\n", (unsigned long)skb);
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	struct Qdisc *q;
	int rc = -ENOMEM;
	bool again = false;

	skb_reset_mac_header(skb);
	skb_assert_len(skb);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_SCHED_TSTAMP))
		__skb_tstamp_tx(skb, NULL, NULL, skb->sk, SCM_TSTAMP_SCHED);

	/* Disable soft irqs for various locks below. Also
	 * stops preemption for RCU.
	 */
	rcu_read_lock_bh();

	skb_update_prio(skb);

	qdisc_pkt_len_init(skb);
	// #ifdef CONFIG_NET_CLS_ACT
	// 	skb->tc_at_ingress = 0;
	// # ifdef CONFIG_NET_EGRESS
	// 	if (static_branch_unlikely(&egress_needed_key)) {
	// 		skb = sch_handle_egress(skb, &rc, dev);
	// 		if (!skb)
	// 			goto out;
	// 	}
	// # endif
	// #endif
	/* If device/qdisc don't need skb->dst, release it right now while
	 * its hot in this cpu cache.
	 */
	if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
		skb_dst_drop(skb);
	else
		skb_dst_force(skb);

	txq = netdev_core_pick_tx(dev, skb, sb_dev);
	q = rcu_dereference_bh(txq->qdisc);

	// trace_net_dev_queue(skb);
	if (q->enqueue) {
		rc = __dev_xmit_skb_copyer(skb, q, dev, txq); //todo: hjk
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	 * loopback, all the sorts of tunnels...

	 * Really, it is unlikely that netif_tx_lock protection is necessary
	 * here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	 * counters.)
	 * However, it is possible, that they rely on protection
	 * made by us here.

	 * Check this and shot the lock. It is not prone from deadlocks.
	 *Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		// printk("dev->flags & IFF_UP\n");
		int cpu = smp_processor_id(); /* ok because BHs are off */

		/* Other cpus might concurrently change txq->xmit_lock_owner
		 * to -1 or to their cpu id, but not to our id.
		 */
		if (READ_ONCE(txq->xmit_lock_owner) != cpu) {
			if (dev_xmit_recursion())
				goto recursion_alert;

			skb = validate_xmit_skb(skb, dev, &again);
			if (!skb)
				goto out;

			PRANDOM_ADD_NOISE(skb, dev, txq, jiffies);
			HARD_TX_LOCK(dev, txq, cpu);

			if (!netif_xmit_stopped(txq)) {
				dev_xmit_recursion_inc();
				skb = dev_hard_start_xmit_copyer(skb, dev, txq, &rc); //todo: hjk
				dev_xmit_recursion_dec();
				if (dev_xmit_complete(rc)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
			net_crit_ratelimited("Virtual device %s asks to queue packet!\n", dev->name);
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately
			 */
		recursion_alert:
			net_crit_ratelimited("Dead loop on virtual device %s, fix it urgently!\n", dev->name);
		}
	}

	rc = -ENETDOWN;
	rcu_read_unlock_bh();

	atomic_long_inc(&dev->tx_dropped);
	kfree_skb_list(skb);
	return rc;
out:
	rcu_read_unlock_bh();
	return rc;
}

static inline int COPYER_WRAP(neigh_hh_output)(const struct hh_cache *hh, struct sk_buff *skb)
{
	// printk("neigh_hh_output skb = %llu\n", (unsigned long)skb);
	unsigned int hh_alen = 0;
	unsigned int seq;
	unsigned int hh_len;

	do {
		seq = read_seqbegin(&hh->hh_lock);
		hh_len = READ_ONCE(hh->hh_len);
		if (likely(hh_len <= HH_DATA_MOD)) {
			hh_alen = HH_DATA_MOD;

			/* skb_push() would proceed silently if we have room for
			 * the unaligned size but not for the aligned size:
			 * check headroom explicitly.
			 */
			if (likely(skb_headroom(skb) >= HH_DATA_MOD)) {
				/* this is inlined by gcc */
				memcpy(skb->data - HH_DATA_MOD, hh->hh_data, HH_DATA_MOD);
			}
		} else {
			hh_alen = HH_DATA_ALIGN(hh_len);

			if (likely(skb_headroom(skb) >= hh_alen)) {
				memcpy(skb->data - hh_alen, hh->hh_data, hh_alen);
			}
		}
	} while (read_seqretry(&hh->hh_lock, seq));

	if (WARN_ON_ONCE(skb_headroom(skb) < hh_alen)) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	__skb_push(skb, hh_len);
	return __dev_queue_xmit_copyer(skb, NULL);
}

static inline int COPYER_WRAP(neigh_output)(struct neighbour *n, struct sk_buff *skb, bool skip_cache)
{
	// printk("neigh_output skb = %llu\n", (unsigned long)skb);
	const struct hh_cache *hh = &n->hh;

	/* n->nud_state and hh->hh_len could be changed under us.
	 * neigh_hh_output() is taking care of the race later.
	 */
	if (!skip_cache && (READ_ONCE(n->nud_state) & NUD_CONNECTED) && READ_ONCE(hh->hh_len))
		return neigh_hh_output_copyer(hh, skb);

	printk("n->output(n, skb) NOT overwrited!\n");
	return n->output(n, skb);
}

static int COPYER_WRAP(ip_finish_output2)(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	// printk("ip_finish_output2 skb = %llu\n", (unsigned long)skb);
	struct dst_entry *dst = skb_dst(skb);
	struct rtable *rt = (struct rtable *)dst;
	struct net_device *dev = dst->dev;
	unsigned int hh_len = LL_RESERVED_SPACE(dev);
	struct neighbour *neigh;
	bool is_v6gw = false;

	if (rt->rt_type == RTN_MULTICAST) {
		IP_UPD_PO_STATS(net, IPSTATS_MIB_OUTMCAST, skb->len);
	} else if (rt->rt_type == RTN_BROADCAST)
		IP_UPD_PO_STATS(net, IPSTATS_MIB_OUTBCAST, skb->len);

	if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
		skb = skb_expand_head(skb, hh_len);
		if (!skb)
			return -ENOMEM;
	}

	if (lwtunnel_xmit_redirect(dst->lwtstate)) {
		int res = lwtunnel_xmit(skb);

		if (res < 0 || res == LWTUNNEL_XMIT_DONE)
			return res;
	}

	rcu_read_lock_bh();
	neigh = ip_neigh_for_gw(rt, skb, &is_v6gw);
	if (!IS_ERR(neigh)) {
		int res;

		sock_confirm_neigh(skb, neigh);
		/* if crossing protocols, can not use the cached header */
		res = neigh_output_copyer(neigh, skb, is_v6gw);
		rcu_read_unlock_bh();
		return res;
	}
	rcu_read_unlock_bh();

	net_dbg_ratelimited("%s: No header cache and no neighbour!\n", __func__);
	kfree_skb(skb);
	return -EINVAL;
}

int COPYER_WRAP(ip_do_fragment)(struct net *net, struct sock *sk, struct sk_buff *skb, int (*output)(struct net *, struct sock *, struct sk_buff *))
{
	struct iphdr *iph;
	struct sk_buff *skb2;
	struct rtable *rt = skb_rtable(skb);
	unsigned int mtu, hlen, ll_rs;
	struct ip_fraglist_iter iter;
	ktime_t tstamp = skb->tstamp;
	struct ip_frag_state state;
	int err = 0;

	/* for offloaded checksums cleanup checksum before fragmentation */
	if (skb->ip_summed == CHECKSUM_PARTIAL && (err = skb_checksum_help(skb)))
		goto fail;

	/*
	 *	Point into the IP datagram header.
	 */

	iph = ip_hdr(skb);

	mtu = ip_skb_dst_mtu(sk, skb);
	if (IPCB(skb)->frag_max_size && IPCB(skb)->frag_max_size < mtu)
		mtu = IPCB(skb)->frag_max_size;

	/*
	 *	Setup starting values.
	 */

	hlen = iph->ihl * 4;
	mtu = mtu - hlen; /* Size of data space */
	IPCB(skb)->flags |= IPSKB_FRAG_COMPLETE;
	ll_rs = LL_RESERVED_SPACE(rt->dst.dev);

	/* When frag_list is given, use it. First, check its validity:
	 * some transformers could create wrong frag_list or break existing
	 * one, it is not prohibited. In this case fall back to copying.
	 *
	 * LATER: this step can be merged to real generation of fragments,
	 * we can switch to copy when see the first bad fragment.
	 */
	if (skb_has_frag_list(skb)) {
		struct sk_buff *frag, *frag2;
		unsigned int first_len = skb_pagelen(skb);

		if (first_len - hlen > mtu || ((first_len - hlen) & 7) || ip_is_fragment(iph) || skb_cloned(skb) || skb_headroom(skb) < ll_rs)
			goto slow_path;

		skb_walk_frags(skb, frag)
		{
			/* Correct geometry. */
			if (frag->len > mtu || ((frag->len & 7) && frag->next) || skb_headroom(frag) < hlen + ll_rs)
				goto slow_path_clean;

			/* Partially cloned skb? */
			if (skb_shared(frag))
				goto slow_path_clean;

			BUG_ON(frag->sk);
			if (skb->sk) {
				frag->sk = skb->sk;
				frag->destructor = sock_wfree;
			}
			skb->truesize -= frag->truesize;
		}

		/* Everything is OK. Generate! */
		ip_fraglist_init(skb, iph, hlen, &iter);

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down. */
			if (iter.frag) {
				bool first_frag = (iter.offset == 0);

				IPCB(iter.frag)->flags = IPCB(skb)->flags;
				ip_fraglist_prepare(skb, &iter);
				if (first_frag && IPCB(skb)->opt.optlen) {
					/* ipcb->opt is not populated for frags
					 * coming from __ip_make_skb(),
					 * ip_options_fragment() needs optlen
					 */
					IPCB(iter.frag)->opt.optlen = IPCB(skb)->opt.optlen;
					ip_options_fragment(iter.frag);
					ip_send_check(iter.iph);
				}
			}

			skb->tstamp = tstamp;
			err = output(net, sk, skb);

			if (!err)
				IP_INC_STATS(net, IPSTATS_MIB_FRAGCREATES);
			if (err || !iter.frag)
				break;

			skb = ip_fraglist_next(&iter);
		}

		if (err == 0) {
			IP_INC_STATS(net, IPSTATS_MIB_FRAGOKS);
			return 0;
		}

		kfree_skb_list(iter.frag);

		IP_INC_STATS(net, IPSTATS_MIB_FRAGFAILS);
		return err;

	slow_path_clean:
		skb_walk_frags(skb, frag2)
		{
			if (frag2 == frag)
				break;
			frag2->sk = NULL;
			frag2->destructor = NULL;
			skb->truesize += frag2->truesize;
		}
	}

slow_path:
	/*
	 *	Fragment the datagram.
	 */
	printk("ip_do_fragment slow path\n");

	ip_frag_init(skb, hlen, ll_rs, mtu, IPCB(skb)->flags & IPSKB_FRAG_PMTU, &state);

	/*
	 *	Keep copying data until we run out.
	 */

	while (state.left > 0) {
		bool first_frag = (state.offset == 0);

		skb2 = ip_frag_next(skb, &state);
		if (IS_ERR(skb2)) {
			err = PTR_ERR(skb2);
			goto fail;
		}
		ip_frag_ipcb(skb, skb2, first_frag);

		/*
		 *	Put this fragment into the sending queue.
		 */
		skb2->tstamp = tstamp;
		err = output(net, sk, skb2);
		if (err)
			goto fail;

		IP_INC_STATS(net, IPSTATS_MIB_FRAGCREATES);
	}
	consume_skb(skb);
	IP_INC_STATS(net, IPSTATS_MIB_FRAGOKS);
	return err;

fail:
	kfree_skb(skb);
	IP_INC_STATS(net, IPSTATS_MIB_FRAGFAILS);
	return err;
}

static int COPYER_WRAP(ip_fragment)(struct net *net, struct sock *sk, struct sk_buff *skb, unsigned int mtu,
				    int (*output)(struct net *, struct sock *, struct sk_buff *))
{
	struct iphdr *iph = ip_hdr(skb);

	if ((iph->frag_off & htons(IP_DF)) == 0)
		return ip_do_fragment_copyer(net, sk, skb, output);

	if (unlikely(!skb->ignore_df || (IPCB(skb)->frag_max_size && IPCB(skb)->frag_max_size > mtu))) {
		IP_INC_STATS(net, IPSTATS_MIB_FRAGFAILS);
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	return ip_do_fragment_copyer(net, sk, skb, output);
}

static int COPYER_WRAP(ip_finish_output_gso)(struct net *net, struct sock *sk, struct sk_buff *skb, unsigned int mtu)
{
	struct sk_buff *segs, *nskb;
	netdev_features_t features;
	int ret = 0;

	/* common case: seglen is <= mtu
	 */
	if (skb_gso_validate_network_len(skb, mtu))
		return ip_finish_output2_copyer(net, sk, skb);

	/* Slowpath -  GSO segment length exceeds the egress MTU.
	 *
	 * This can happen in several cases:
	 *  - Forwarding of a TCP GRO skb, when DF flag is not set.
	 *  - Forwarding of an skb that arrived on a virtualization interface
	 *    (virtio-net/vhost/tap) with TSO/GSO size set by other network
	 *    stack.
	 *  - Local GSO skb transmitted on an NETIF_F_TSO tunnel stacked over an
	 *    interface with a smaller MTU.
	 *  - Arriving GRO skb (or GSO skb in a virtualized environment) that is
	 *    bridged to a NETIF_F_TSO tunnel stacked over an interface with an
	 *    insufficient MTU.
	 */
	features = netif_skb_features(skb);
	BUILD_BUG_ON(sizeof(*IPCB(skb)) > SKB_GSO_CB_OFFSET);
	segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);
	if (IS_ERR_OR_NULL(segs)) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	consume_skb(skb);

	skb_list_walk_safe(segs, segs, nskb)
	{
		int err;

		skb_mark_not_on_list(segs);
		err = ip_fragment_copyer(net, sk, segs, mtu, ip_finish_output2_copyer);

		if (err && ret == 0)
			ret = err;
	}

	return ret;
}

static int COPYER_WRAP(__ip_finish_output)(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	// printk("__ip_finish_output skb = %llu\n", (unsigned long)skb);
	unsigned int mtu;

	mtu = ip_skb_dst_mtu(sk, skb);
	if (skb_is_gso(skb))
		return ip_finish_output_gso_copyer(net, sk, skb, mtu);

	if (skb->len > mtu || IPCB(skb)->frag_max_size)
		return ip_fragment_copyer(net, sk, skb, mtu, ip_finish_output2_copyer);

	return ip_finish_output2_copyer(net, sk, skb);
}

int COPYER_WRAP(ip_output)(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	// printk("ip_output skb = %llu\n", (unsigned long)skb);
	struct net_device *dev = skb_dst(skb)->dev, *indev = skb->dev;

	IP_UPD_PO_STATS(net, IPSTATS_MIB_OUT, skb->len);

	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	return __ip_finish_output_copyer(net, sk, skb);
	// NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING, net, sk, skb, indev, dev, ip_finish_output,
	//              !(IPCB(skb)->flags & IPSKB_REROUTED));
}

int COPYER_WRAP(__ip_local_out)(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

	iph->tot_len = htons(skb->len);
	ip_send_check(iph);

	/* if egress device is enslaved to an L3 master device pass the
	 * skb to its handler for processing
	 */
	skb = l3mdev_ip_out(sk, skb);
	if (unlikely(!skb))
		return 0;

	skb->protocol = htons(ETH_P_IP);

	return nf_hook(NFPROTO_IPV4, NF_INET_LOCAL_OUT, net, sk, skb, NULL, skb_dst(skb)->dev, ip_output_copyer);
}

int COPYER_WRAP(ip_local_out)(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	// printk("ip_local_out skb = %llu\n", (unsigned long)skb);
	int err;

	err = __ip_local_out_copyer(net, sk, skb);
	if (likely(err == 1))
		err = ip_output_copyer(net, sk, skb);

	return err;
}

int COPYER_WRAP(__ip_queue_xmit)(struct sock *sk, struct sk_buff *skb, struct flowi *fl, __u8 tos)
{
	// printk("__ip_queue_xmit skb = %llu\n", (unsigned long)skb);
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	struct ip_options_rcu *inet_opt;
	struct flowi4 *fl4;
	struct rtable *rt;
	struct iphdr *iph;
	int res;

	/* Skip all of this if the packet is already routed,
	 * f.e. by something like SCTP.
	 */
	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	fl4 = &fl->u.ip4;
	rt = skb_rtable(skb);
	if (rt)
		goto packet_routed;

	/* Make sure we can route this packet. */
	rt = (struct rtable *)__sk_dst_check(sk, 0);
	if (!rt) {
		__be32 daddr;

		/* Use correct destination address if we have options. */
		daddr = inet->inet_daddr;
		if (inet_opt && inet_opt->opt.srr)
			daddr = inet_opt->opt.faddr;

		/* If this fails, retransmit mechanism of transport layer will
		 * keep trying until route appears or the connection times
		 * itself out.
		 */
		rt = ip_route_output_ports(net, fl4, sk, daddr, inet->inet_saddr, inet->inet_dport, inet->inet_sport, sk->sk_protocol,
					   RT_CONN_FLAGS_TOS(sk, tos), sk->sk_bound_dev_if);
		if (IS_ERR(rt))
			goto no_route;
		sk_setup_caps(sk, &rt->dst);
	}
	skb_dst_set_noref(skb, &rt->dst);

packet_routed:
	// printk("packet_routed\n");
	if (inet_opt && inet_opt->opt.is_strictroute && rt->rt_uses_gateway)
		goto no_route;

	/* OK, we know where to send it, allocate and build IP header. */
	skb_push(skb, sizeof(struct iphdr) + (inet_opt ? inet_opt->opt.optlen : 0));
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	*((__be16 *)iph) = htons((4 << 12) | (5 << 8) | (tos & 0xff));
	if (ip_dont_fragment(sk, &rt->dst) && !skb->ignore_df)
		iph->frag_off = htons(IP_DF);
	else
		iph->frag_off = 0;
	iph->ttl = ip_select_ttl(inet, &rt->dst);
	iph->protocol = sk->sk_protocol;
	ip_copy_addrs(iph, fl4);

	/* Transport layer set skb->h.foo itself. */

	if (inet_opt && inet_opt->opt.optlen) {
		iph->ihl += inet_opt->opt.optlen >> 2;
		ip_options_build(skb, &inet_opt->opt, inet->inet_daddr, rt, 0);
	}

	ip_select_ident_segs(net, skb, sk, skb_shinfo(skb)->gso_segs ?: 1);

	/* TODO : should we use skb->sk here instead of sk ? */
	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

	res = ip_local_out_copyer(net, sk, skb);
	rcu_read_unlock();
	return res;

no_route:
	rcu_read_unlock();
	IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	return -EHOSTUNREACH;
}

/* This routine actually transmits TCP packets queued in by
 * tcp_do_sendmsg().  This is used by both the initial
 * transmission and possible later retransmissions.
 * All SKB's seen here are completely headerless.  It is our
 * job to build the TCP header, and pass the packet down to
 * IP so it can do the same plus pass the packet off to the
 * device.
 *
 * We are working here with either a clone of the original
 * SKB, or a fresh unique copy made by the retransmit engine.
 */
static int COPYER_WRAP(__tcp_transmit_skb)(struct sock *sk, struct sk_buff *skb, int clone_it, gfp_t gfp_mask, u32 rcv_nxt)
{
	// printk("__tcp_transmit_skb skb = %llu\n", (unsigned long)skb);
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet;
	struct tcp_sock *tp;
	struct tcp_skb_cb *tcb;
	struct tcp_out_options opts;
	unsigned int tcp_options_size, tcp_header_size;
	struct sk_buff *oskb = NULL;
	struct tcp_md5sig_key *md5;
	struct tcphdr *th;
	u64 prior_wstamp;
	int err;

	BUG_ON(!skb || !tcp_skb_pcount(skb));
	tp = tcp_sk(sk);
	prior_wstamp = tp->tcp_wstamp_ns;
	tp->tcp_wstamp_ns = max(tp->tcp_wstamp_ns, tp->tcp_clock_cache);
	skb->skb_mstamp_ns = tp->tcp_wstamp_ns;
	if (clone_it) {
		TCP_SKB_CB(skb)->tx.in_flight = TCP_SKB_CB(skb)->end_seq - tp->snd_una;
		oskb = skb;

		tcp_skb_tsorted_save(oskb)
		{
			if (unlikely(skb_cloned(oskb)))
				skb = pskb_copy(oskb, gfp_mask);
			else
				skb = skb_clone(oskb, gfp_mask);
			skb->descriptor = oskb->descriptor;
		}
		tcp_skb_tsorted_restore(oskb);

		if (unlikely(!skb))
			return -ENOBUFS;
		/* retransmit skbs might have a non zero value in skb->dev
		 * because skb->dev is aliased with skb->rbnode.rb_left
		 */
		skb->dev = NULL;
	}

	inet = inet_sk(sk);
	tcb = TCP_SKB_CB(skb);
	memset(&opts, 0, sizeof(opts));

	if (unlikely(tcb->tcp_flags & TCPHDR_SYN)) {
		tcp_options_size = tcp_syn_options(sk, skb, &opts, &md5);
	} else {
		tcp_options_size = tcp_established_options(sk, skb, &opts, &md5);
		/* Force a PSH flag on all (GSO) packets to expedite GRO flush
		 * at receiver : This slightly improve GRO performance.
		 * Note that we do not force the PSH flag for non GSO packets,
		 * because they might be sent under high congestion events,
		 * and in this case it is better to delay the delivery of 1-MSS
		 * packets and thus the corresponding ACK packet that would
		 * release the following packet.
		 */
		if (tcp_skb_pcount(skb) > 1)
			tcb->tcp_flags |= TCPHDR_PSH;
	}
	tcp_header_size = tcp_options_size + sizeof(struct tcphdr);

	/* if no packet is in qdisc/device queue, then allow XPS to select
	 * another queue. We can be called from tcp_tsq_handler()
	 * which holds one reference to sk.
	 *
	 * TODO: Ideally, in-flight pure ACK packets should not matter here.
	 * One way to get this would be to set skb->truesize = 2 on them.
	 */
	skb->ooo_okay = sk_wmem_alloc_get(sk) < SKB_TRUESIZE(1);

	/* If we had to use memory reserve to allocate this skb,
	 * this might cause drops if packet is looped back :
	 * Other socket might not have SOCK_MEMALLOC.
	 * Packets not looped back do not care about pfmemalloc.
	 */
	skb->pfmemalloc = 0;

	skb_push(skb, tcp_header_size);
	skb_reset_transport_header(skb);

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = skb_is_tcp_pure_ack(skb) ? __sock_wfree : tcp_wfree;
	refcount_add(skb->truesize, &sk->sk_wmem_alloc);

	skb_set_dst_pending_confirm(skb, sk->sk_dst_pending_confirm);

	/* Build TCP header and checksum it. */
	th = (struct tcphdr *)skb->data;
	th->source = inet->inet_sport;
	th->dest = inet->inet_dport;
	th->seq = htonl(tcb->seq);
	th->ack_seq = htonl(rcv_nxt);
	*(((__be16 *)th) + 6) = htons(((tcp_header_size >> 2) << 12) | tcb->tcp_flags);

	th->check = 0;
	th->urg_ptr = 0;

	/* The urg_mode check is necessary during a below snd_una win probe */
	if (unlikely(tcp_urg_mode(tp) && before(tcb->seq, tp->snd_up))) {
		if (before(tp->snd_up, tcb->seq + 0x10000)) {
			th->urg_ptr = htons(tp->snd_up - tcb->seq);
			th->urg = 1;
		} else if (after(tcb->seq + 0xFFFF, tp->snd_nxt)) {
			th->urg_ptr = htons(0xFFFF);
			th->urg = 1;
		}
	}

	skb_shinfo(skb)->gso_type = sk->sk_gso_type;
	if (likely(!(tcb->tcp_flags & TCPHDR_SYN))) {
		th->window = htons(tcp_select_window(sk));
		tcp_ecn_send(sk, skb, th, tcp_header_size);
	} else {
		/* RFC1323: The window in SYN & SYN/ACK segments
		 * is never scaled.
		 */
		th->window = htons(min(tp->rcv_wnd, 65535U));
	}

	tcp_options_write((__be32 *)(th + 1), tp, &opts);

	// #ifdef CONFIG_TCP_MD5SIG
	// 	/* Calculate the MD5 hash, as we have all we need now */
	// 	if (md5) {
	// 		sk_nocaps_add(sk, NETIF_F_GSO_MASK);
	// 		tp->af_specific->calc_md5_hash(opts.hash_location, md5, sk, skb);
	// 	}
	// #endif

	/* BPF prog is the last one writing header option */
	// bpf_skops_write_hdr_opt(sk, skb, NULL, NULL, 0, &opts);

	tcp_v4_send_check(sk, skb);

	if (likely(tcb->tcp_flags & TCPHDR_ACK))
		tcp_event_ack_sent(sk, tcp_skb_pcount(skb), rcv_nxt);

	if (skb->len != tcp_header_size) {
		tcp_event_data_sent(tp, sk);
		tp->data_segs_out += tcp_skb_pcount(skb);
		tp->bytes_sent += skb->len - tcp_header_size;
	}

	if (after(tcb->end_seq, tp->snd_nxt) || tcb->seq == tcb->end_seq)
		TCP_ADD_STATS(sock_net(sk), TCP_MIB_OUTSEGS, tcp_skb_pcount(skb));

	tp->segs_out += tcp_skb_pcount(skb);
	skb_set_hash_from_sk(skb, sk);
	/* OK, its time to fill skb_shinfo(skb)->gso_{segs|size} */
	skb_shinfo(skb)->gso_segs = tcp_skb_pcount(skb);
	skb_shinfo(skb)->gso_size = tcp_skb_mss(skb);

	/* Leave earliest departure time in skb->tstamp (skb->skb_mstamp_ns) */

	/* Cleanup our debris for IP stacks */
	memset(skb->cb, 0, max(sizeof(struct inet_skb_parm), sizeof(struct inet6_skb_parm)));

	tcp_add_tx_delay(skb, tp);

	err = __ip_queue_xmit_copyer(sk, skb, &inet->cork.fl, inet_sk(sk)->tos); //hjk

	if (unlikely(err > 0)) {
		tcp_enter_cwr(sk);
		err = net_xmit_eval(err);
	}
	if (!err && oskb) {
		tcp_update_skb_after_send(sk, oskb, prior_wstamp);
		tcp_rate_skb_sent(sk, oskb);
	}
	return err;
}

/* This routine writes packets to the network.  It advances the
 * send_head.  This happens as incoming acks open up the remote
 * window for us.
 *
 * LARGESEND note: !tcp_urg_mode is overkill, only frames between
 * snd_up-64k-mss .. snd_up cannot be large. However, taking into
 * account rare use of URG, this is not a big flaw.
 *
 * Send at most one packet when push_one > 0. Temporarily ignore
 * cwnd limit to force at most one packet out when push_one == 2.

 * Returns true, if no segments are in flight and we have queued segments,
 * but cannot send anything now because of SWS or another problem.
 */
static bool COPYER_WRAP(tcp_write_xmit)(struct sock *sk, unsigned int mss_now, int nonagle, int push_one, gfp_t gfp)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	unsigned int tso_segs, sent_pkts;
	int cwnd_quota;
	int result;
	bool is_cwnd_limited = false, is_rwnd_limited = false;
	u32 max_segs;

	sent_pkts = 0;

	tcp_mstamp_refresh(tp);
	if (!push_one) {
		/* Do MTU probing. */
		result = tcp_mtu_probe(sk);
		if (!result) {
			return false;
		} else if (result > 0) {
			sent_pkts = 1;
		}
	}

	max_segs = tcp_tso_segs(sk, mss_now);
	while ((skb = tcp_send_head(sk))) {
		// printk("tcp_write_xmit skb = %llu\n", (unsigned long)skb);
		unsigned int limit;

		if (unlikely(tp->repair) && tp->repair_queue == TCP_SEND_QUEUE) {
			/* "skb_mstamp_ns" is used as a start point for the retransmit timer */
			skb->skb_mstamp_ns = tp->tcp_wstamp_ns = tp->tcp_clock_cache;
			list_move_tail(&skb->tcp_tsorted_anchor, &tp->tsorted_sent_queue);
			tcp_init_tso_segs(skb, mss_now);
			goto repair; /* Skip network transmission */
		}

		if (tcp_pacing_check(sk))
			break;

		tso_segs = tcp_init_tso_segs(skb, mss_now);
		BUG_ON(!tso_segs);

		cwnd_quota = tcp_cwnd_test(tp, skb);
		if (!cwnd_quota) {
			if (push_one == 2)
				/* Force out a loss probe pkt. */
				cwnd_quota = 1;
			else
				break;
		}

		if (unlikely(!tcp_snd_wnd_test(tp, skb, mss_now))) {
			is_rwnd_limited = true;
			break;
		}

		if (tso_segs == 1) {
			if (unlikely(!tcp_nagle_test(tp, skb, mss_now, (tcp_skb_is_last(sk, skb) ? nonagle : TCP_NAGLE_PUSH)))) {
				COPIER_WAIT(skb);
				break;
			}
		} else {
			if (!push_one && tcp_tso_should_defer(sk, skb, &is_cwnd_limited, &is_rwnd_limited, max_segs))
				break;
		}

		limit = mss_now;
		if (tso_segs > 1 && !tcp_urg_mode(tp))
			limit = tcp_mss_split_point(sk, skb, mss_now, min_t(unsigned int, cwnd_quota, max_segs), nonagle);

		if (skb->len > limit && unlikely(tso_fragment(sk, skb, limit, mss_now, gfp)))
			break;

		if (tcp_small_queue_check(sk, skb, 0))
			break;

		/* Argh, we hit an empty skb(), presumably a thread
		 * is sleeping in sendmsg()/sk_stream_wait_memory().
		 * We do not want to send a pure-ack packet and have
		 * a strange looking rtx queue with empty packet(s).
		 */
		if (TCP_SKB_CB(skb)->end_seq == TCP_SKB_CB(skb)->seq)
			break;

		if (unlikely(__tcp_transmit_skb_copyer(sk, skb, 1, gfp, tcp_sk(sk)->rcv_nxt)))
			break;

	repair:
		/* Advance the send_head.  This one is sent out.
		 * This call will increment packets_out.
		 */
		tcp_event_new_data_sent(sk, skb);

		tcp_minshall_update(tp, mss_now, skb);
		sent_pkts += tcp_skb_pcount(skb);

		if (push_one)
			break;
	}

	if (is_rwnd_limited)
		tcp_chrono_start(sk, TCP_CHRONO_RWND_LIMITED);
	else
		tcp_chrono_stop(sk, TCP_CHRONO_RWND_LIMITED);

	is_cwnd_limited |= (tcp_packets_in_flight(tp) >= tcp_snd_cwnd(tp));
	if (likely(sent_pkts || is_cwnd_limited))
		tcp_cwnd_validate(sk, is_cwnd_limited);

	if (likely(sent_pkts)) {
		if (tcp_in_cwnd_reduction(sk))
			tp->prr_out += sent_pkts;

		/* Send one loss probe per tail loss episode. */
		if (push_one != 2)
			tcp_schedule_loss_probe(sk, false);
		return false;
	}
	return !tp->packets_out && !tcp_write_queue_empty(sk);
}

/* Send _single_ skb sitting at the send head. This function requires
 * true push pending frames to setup probe timer etc.
 */
void COPYER_WRAP(tcp_push_one)(struct sock *sk, unsigned int mss_now)
{
	struct sk_buff *skb = tcp_send_head(sk);

	BUG_ON(!skb || skb->len < mss_now);

	tcp_write_xmit_copyer(sk, mss_now, TCP_NAGLE_PUSH, 1, sk->sk_allocation);
}

void COPYER_WRAP(__tcp_push_pending_frames)(struct sock *sk, unsigned int cur_mss, int nonagle)
{
	/* If we are closed, the bytes will have to remain here.
	 * In time closedown will finish, we empty the write queue and
	 * all will be happy.
	 */
	if (unlikely(sk->sk_state == TCP_CLOSE))
		return;

	if (tcp_write_xmit_copyer(sk, cur_mss, nonagle, 0, sk_gfp_mask(sk, GFP_ATOMIC)))
		tcp_check_probe_timer(sk);
}

void COPYER_WRAP(tcp_push)(struct sock *sk, int flags, int mss_now, int nonagle, int size_goal)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;

	skb = tcp_write_queue_tail(sk);
	if (!skb)
		return;
	if (!(flags & MSG_MORE) || forced_push(tp))
		tcp_mark_push(tp, skb);

	tcp_mark_urg(tp, flags);

	if (tcp_should_autocork(sk, skb, size_goal)) {
		/* avoid atomic op if TSQ_THROTTLED bit is already set */
		if (!test_bit(TSQ_THROTTLED, &sk->sk_tsq_flags)) {
			NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTOCORKING);
			set_bit(TSQ_THROTTLED, &sk->sk_tsq_flags);
		}
		/* It is possible TX completion already happened
		 * before we set TSQ_THROTTLED.
		 */
		if (refcount_read(&sk->sk_wmem_alloc) > skb->truesize)
			return;
	}

	if (flags & MSG_MORE)
		nonagle = TCP_NAGLE_CORK;

	__tcp_push_pending_frames_copyer(sk, mss_now, nonagle);
}

static int add_copyin_to_queue(struct cp_queue_kernel *q, void *to, void __user *from, size_t n, volatile uint8_t *descriptor)
{
	// printk("async %lu bytes, q = %lu\n", n, (unsigned long)q);
	unsigned int write_index;
	struct cp_task *task;

	spin_lock(&q->lock);
	write_index = q->write_index;
	task = &q->tasks[write_index];
	if (task->status == STATUS_WAITING) {
		printk("full in add_entry_to_cp_in_queue\n");
		return n;
	} else {
		task->descriptor_byte = descriptor;
		task->from_va = from;
		task->type = TYPE_SEND_DATA;
		task->to_va = to;
		task->length = n;
		task->status = STATUS_WAITING;
	}
	q->write_index = (write_index + 1) % QUEUE_LEN;
	spin_unlock(&q->lock);
	return 0;
}

static __always_inline __must_check size_t COPYER_WRAP(copy_from_iter)(struct cp_queue_kernel *q, void *addr, size_t bytes, struct iov_iter *i,
								       volatile uint8_t *descriptor)
{
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

#define __iterate_and_advance(i, n, base, len, off, I)                                                                                               \
	{                                                                                                                                            \
		if (unlikely(i->count < n))                                                                                                          \
			n = i->count;                                                                                                                \
		if (likely(n)) {                                                                                                                     \
			if (likely(iter_is_iovec(i))) {                                                                                              \
				const struct iovec *iov = i->iov;                                                                                    \
				void __user *base;                                                                                                   \
				size_t len;                                                                                                          \
				iterate_iovec(i, n, base, len, off, iov, (I)) i->nr_segs -= iov - i->iov;                                            \
				i->iov = iov;                                                                                                        \
			}                                                                                                                            \
			i->count -= n;                                                                                                               \
		}                                                                                                                                    \
	}

	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else {
		if (unlikely(iov_iter_is_pipe(i))) {
			WARN_ON(1);
			return 0;
		}
		if (iter_is_iovec(i))
			might_fault();
		__iterate_and_advance(i, bytes, base, len, off, add_copyin_to_queue(q, addr + off, base, len, descriptor))

			return bytes;
	}
}

static __always_inline __must_check bool COPYER_WRAP(copy_from_iter_full)(struct cp_queue_kernel *q, void *addr, size_t bytes, struct iov_iter *i,
									  volatile uint8_t *descriptor)
{
	size_t copied = copy_from_iter_copyer(q, addr, bytes, i, descriptor);
	if (likely(copied == bytes))
		return true;
	iov_iter_revert(i, copied);
	return false;
}

static inline int COPYER_WRAP(skb_do_copy_data_nocache)(struct cp_queue_kernel *q, struct sock *sk, struct sk_buff *skb, struct iov_iter *from,
							char *to, int copy, int offset)
{
	// printk("skb_do_copy_data_nocache skb = %llu\n", (unsigned long)skb);
	skb->descriptor = kmalloc(sizeof(u8), GFP_KERNEL);

	if (skb->ip_summed == CHECKSUM_NONE) {
		__wsum csum = 0;
		printk("not implemented\n");
		if (!csum_and_copy_from_iter_full(to, copy, &csum, from))
			return -EFAULT;
		skb->csum = csum_block_add(skb->csum, csum, offset);
	} else if (sk->sk_route_caps & NETIF_F_NOCACHE_COPY) {
		printk("not implemented\n");
		if (!copy_from_iter_full_nocache(to, copy, from))
			return -EFAULT;
	} else if (!copy_from_iter_full_copyer(q, to, copy, from, skb->descriptor)) {
		return -EFAULT;
	}

	return 0;
}

static inline int COPYER_WRAP(skb_copy_to_page_nocache)(struct cp_queue_kernel *q, struct sock *sk, struct iov_iter *from, struct sk_buff *skb,
							struct page *page, int off, int copy)
{
	int err;

	err = skb_do_copy_data_nocache_copyer(q, sk, skb, from, page_address(page) + off, copy, skb->len);
	if (err)
		return err;

	skb->len += copy;
	skb->data_len += copy;
	skb->truesize += copy;
	sk_wmem_queued_add(sk, copy);
	sk_mem_charge(sk, copy);
	return 0;
}

static inline int COPYER_WRAP(skb_add_data_nocache)(struct cp_queue_kernel *q, struct sock *sk, struct sk_buff *skb, struct iov_iter *from, int copy)
{
	int err, offset = skb->len;

	err = skb_do_copy_data_nocache_copyer(q, sk, skb, from, skb_put(skb, copy), copy, offset);
	if (err)
		__skb_trim(skb, offset);

	return err;
}

int COPYER_WRAP(tcp_sendmsg_locked)(struct sock *sk, struct msghdr *msg, size_t size, int copier_fd)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ubuf_info *uarg = NULL;
	struct sk_buff *skb;
	struct sockcm_cookie sockc;
	int flags, err, copied = 0;
	int mss_now = 0, size_goal, copied_syn = 0;
	int process_backlog = 0;
	bool zc = false;
	long timeo;

	struct process_queues *queues = ((struct copier_ctx *)(fget(copier_fd)->private_data))->queues;
	struct cp_queue_kernel *q = &queues->kernel_cp_queue;
	struct cp_queue *uq = &queues->user_cp_queue;

	add_start_barrier(q, uq);

	flags = msg->msg_flags;

	// if (flags & MSG_ZEROCOPY && size && sock_flag(sk, SOCK_ZEROCOPY)) {
	// 	skb = tcp_write_queue_tail(sk);
	// 	uarg = msg_zerocopy_realloc(sk, size, skb_zcopy(skb));
	// 	if (!uarg) {
	// 		err = -ENOBUFS;
	// 		goto out_err;
	// 	}

	// 	zc = sk->sk_route_caps & NETIF_F_SG;
	// 	if (!zc)
	// 		uarg->zerocopy = 0;
	// }

	// if (unlikely(flags & MSG_FASTOPEN || inet_sk(sk)->defer_connect) &&
	//     !tp->repair) {
	// 	err = tcp_sendmsg_fastopen(sk, msg, &copied_syn, size, uarg);
	// 	if (err == -EINPROGRESS && copied_syn > 0)
	// 		goto out;
	// 	else if (err)
	// 		goto out_err;
	// }

	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);

	tcp_rate_check_app_limited(sk); /* is sending application-limited? */

	/* Wait for a connection to finish. One exception is TCP Fast Open
	 * (passive side) where data is allowed to be sent before a connection
	 * is fully established.
	 */
	if (((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) && !tcp_passive_fastopen(sk)) {
		err = sk_stream_wait_connect(sk, &timeo);
		if (err != 0)
			goto do_error;
	}

	if (unlikely(tp->repair)) {
		if (tp->repair_queue == TCP_RECV_QUEUE) {
			copied = tcp_send_rcvq(sk, msg, size);
			goto out_nopush;
		}

		err = -EINVAL;
		if (tp->repair_queue == TCP_NO_QUEUE)
			goto out_err;

		/* 'common' sending to sendq */
	}

	sockcm_init(&sockc, sk);
	// if (msg->msg_controllen) {
	// 	err = sock_cmsg_send(sk, msg, &sockc);
	// 	if (unlikely(err)) {
	// 		err = -EINVAL;
	// 		goto out_err;
	// 	}
	// }

	/* This should be in poll */
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	/* Ok commence sending. */
	copied = 0;

restart:
	mss_now = tcp_send_mss(sk, &size_goal, flags);

	err = -EPIPE;
	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
		goto do_error;

	while (msg_data_left(msg)) {
		int copy = 0;

		skb = tcp_write_queue_tail(sk);
		if (skb)
			copy = size_goal - skb->len;

		if (copy <= 0 || !tcp_skb_can_collapse_to(skb)) {
			bool first_skb;

		new_segment:
			if (!sk_stream_memory_free(sk))
				goto wait_for_space;

			if (unlikely(process_backlog >= 16)) {
				process_backlog = 0;
				if (sk_flush_backlog(sk))
					goto restart;
			}
			first_skb = tcp_rtx_and_write_queues_empty(sk);
			skb = sk_stream_alloc_skb(sk, 0, sk->sk_allocation, first_skb);
			if (!skb)
				goto wait_for_space;

			process_backlog++;
			skb->ip_summed = CHECKSUM_PARTIAL;

			tcp_skb_entail(sk, skb);
			copy = size_goal;

			/* All packets are restored as if they have
			 * already been sent. skb_mstamp_ns isn't set to
			 * avoid wrong rtt estimation.
			 */
			if (tp->repair)
				TCP_SKB_CB(skb)->sacked |= TCPCB_REPAIRED;
		}

		/* Try to append data to the end of skb. */
		if (copy > msg_data_left(msg))
			copy = msg_data_left(msg);

		/* Where to copy to? */
		if (skb_availroom(skb) > 0 && !zc) {
			/* We have some space in skb head. Superb! */
			copy = min_t(int, copy, skb_availroom(skb));
			err = skb_add_data_nocache_copyer(q, sk, skb, &msg->msg_iter, copy); // hjk: todo
			if (err)
				goto do_fault;
		} else if (!zc) {
			bool merge = true;
			int i = skb_shinfo(skb)->nr_frags;
			struct page_frag *pfrag = sk_page_frag(sk);

			if (!sk_page_frag_refill(sk, pfrag))
				goto wait_for_space;

			if (!skb_can_coalesce(skb, i, pfrag->page, pfrag->offset)) {
				if (i >= READ_ONCE(sysctl_max_skb_frags)) {
					tcp_mark_push(tp, skb);
					goto new_segment;
				}
				merge = false;
			}

			copy = min_t(int, copy, pfrag->size - pfrag->offset);

			if (!sk_wmem_schedule(sk, copy))
				goto wait_for_space;

			err = skb_copy_to_page_nocache_copyer(q, sk, &msg->msg_iter, skb, pfrag->page, pfrag->offset,
							      copy); // hjk: todo
			// printk("skb %lld,copy = %d pfrag->size = %d\n", (unsigned long long)skb, copy, pfrag->size);
			if (err)
				goto do_error;

			/* Update the skb. */
			if (merge) {
				skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy);
			} else {
				skb_fill_page_desc(skb, i, pfrag->page, pfrag->offset, copy);
				page_ref_inc(pfrag->page);
			}
			pfrag->offset += copy;
		}
		// else {
		// 	if (!sk_wmem_schedule(sk, copy))
		// 		goto wait_for_space;

		// 	err = skb_zerocopy_iter_stream(sk, skb, msg, copy, uarg);
		// 	if (err == -EMSGSIZE || err == -EEXIST) {
		// 		tcp_mark_push(tp, skb);
		// 		goto new_segment;
		// 	}
		// 	if (err < 0)
		// 		goto do_error;
		// 	copy = err;
		// }

		if (!copied)
			TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_PSH;

		WRITE_ONCE(tp->write_seq, tp->write_seq + copy);
		TCP_SKB_CB(skb)->end_seq += copy;
		tcp_skb_pcount_set(skb, 0);

		copied += copy;
		if (!msg_data_left(msg)) {
			if (unlikely(flags & MSG_EOR))
				TCP_SKB_CB(skb)->eor = 1;
			goto out;
		}

		if (skb->len < size_goal || (flags & MSG_OOB) || unlikely(tp->repair))
			continue;

		if (forced_push(tp)) {
			tcp_mark_push(tp, skb);
			__tcp_push_pending_frames_copyer(sk, mss_now, TCP_NAGLE_PUSH);
		} else if (skb == tcp_send_head(sk))
			tcp_push_one_copyer(sk, mss_now);
		continue;

	wait_for_space:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		if (copied)
			tcp_push(sk, flags & ~MSG_MORE, mss_now, TCP_NAGLE_PUSH, size_goal);

		err = sk_stream_wait_memory(sk, &timeo);
		if (err != 0)
			goto do_error;

		mss_now = tcp_send_mss(sk, &size_goal, flags);
	}

out:
	/* WARNING: No consideration of error handling */

	add_end_barrier(q);

	if (copied) {
		tcp_tx_timestamp(sk, sockc.tsflags);
		tcp_push_copyer(sk, flags, mss_now, tp->nonagle, size_goal);
	}
out_nopush:
	net_zcopy_put(uarg);
	return copied + copied_syn;

do_error:
	skb = tcp_write_queue_tail(sk);
do_fault:
	tcp_remove_empty_skb(sk, skb);

	if (copied + copied_syn)
		goto out;
out_err:
	add_end_barrier(q);
	net_zcopy_put_abort(uarg, true);
	err = sk_stream_error(sk, flags, err);
	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(tcp_rtx_and_write_queues_empty(sk) && err == -EAGAIN)) {
		sk->sk_write_space(sk);
		tcp_chrono_stop(sk, TCP_CHRONO_SNDBUF_LIMITED);
	}
	return err;
}

int COPYER_WRAP(tcp_sendmsg)(struct sock *sk, struct msghdr *msg, size_t size, int copier_fd)
{
	int ret;

	lock_sock(sk);
	ret = tcp_sendmsg_locked_copyer(sk, msg, size, copier_fd);
	release_sock(sk);

	return ret;
}

int COPYER_WRAP(inet_sendmsg)(struct socket *sock, struct msghdr *msg, size_t size, int copier_fd)
{
	struct sock *sk = sock->sk;

	if (unlikely(inet_send_prepare(sk)))
		return -EAGAIN;

	return tcp_sendmsg_copyer(sk, msg, size, copier_fd);
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */
int COPYER_WRAP(__sys_sendto)(int fd, void __user *buff, size_t len, unsigned int flags, struct sockaddr __user *addr, int addr_len, int copier_fd)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err;
	struct msghdr msg;
	struct iovec iov;
	int fput_needed;

	err = import_single_range(WRITE, buff, len, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	if (addr) {
		err = move_addr_to_kernel(addr, addr_len, &address);
		if (err < 0)
			goto out_put;
		msg.msg_name = (struct sockaddr *)&address;
		msg.msg_namelen = addr_len;
	}
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags = flags;
	err = inet_sendmsg_copyer(sock, &msg, msg_data_left(&msg), copier_fd);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE5(sendto_copyer, int, fd, void __user *, buff, size_t, len, unsigned int, flags, int, copier_fd)
{
	return __sys_sendto_copyer(fd, buff, len, flags, NULL, 0, copier_fd);
}

int tcp_sendmsg_locked_copyer_lazy(struct sock *sk, struct msghdr *msg, size_t size, int lazy_fd)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ubuf_info *uarg = NULL;
	struct sk_buff *skb;
	struct sockcm_cookie sockc;
	int flags, err, copied = 0;
	int mss_now = 0, size_goal, copied_syn = 0;
	int process_backlog = 0;
	bool zc = false;
	long timeo;

	struct cp_queue_kernel *q = &((struct copier_ctx *)(fget(lazy_fd)->private_data))->queues->kernel_cp_queue;

	flags = msg->msg_flags;

	// if (flags & MSG_ZEROCOPY && size && sock_flag(sk, SOCK_ZEROCOPY)) {
	// 	skb = tcp_write_queue_tail(sk);
	// 	uarg = msg_zerocopy_realloc(sk, size, skb_zcopy(skb));
	// 	if (!uarg) {
	// 		err = -ENOBUFS;
	// 		goto out_err;
	// 	}

	// 	zc = sk->sk_route_caps & NETIF_F_SG;
	// 	if (!zc)
	// 		uarg->zerocopy = 0;
	// }

	// if (unlikely(flags & MSG_FASTOPEN || inet_sk(sk)->defer_connect) &&
	//     !tp->repair) {
	// 	err = tcp_sendmsg_fastopen(sk, msg, &copied_syn, size, uarg);
	// 	if (err == -EINPROGRESS && copied_syn > 0)
	// 		goto out;
	// 	else if (err)
	// 		goto out_err;
	// }

	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);

	tcp_rate_check_app_limited(sk); /* is sending application-limited? */

	/* Wait for a connection to finish. One exception is TCP Fast Open
	 * (passive side) where data is allowed to be sent before a connection
	 * is fully established.
	 */
	if (((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) && !tcp_passive_fastopen(sk)) {
		err = sk_stream_wait_connect(sk, &timeo);
		if (err != 0)
			goto do_error;
	}

	if (unlikely(tp->repair)) {
		if (tp->repair_queue == TCP_RECV_QUEUE) {
			copied = tcp_send_rcvq(sk, msg, size);
			goto out_nopush;
		}

		err = -EINVAL;
		if (tp->repair_queue == TCP_NO_QUEUE)
			goto out_err;

		/* 'common' sending to sendq */
	}

	sockcm_init(&sockc, sk);
	// if (msg->msg_controllen) {
	// 	err = sock_cmsg_send(sk, msg, &sockc);
	// 	if (unlikely(err)) {
	// 		err = -EINVAL;
	// 		goto out_err;
	// 	}
	// }

	/* This should be in poll */
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	/* Ok commence sending. */
	copied = 0;

restart:
	mss_now = tcp_send_mss(sk, &size_goal, flags);

	err = -EPIPE;
	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
		goto do_error;

	while (msg_data_left(msg)) {
		int copy = 0;

		skb = tcp_write_queue_tail(sk);
		if (skb)
			copy = size_goal - skb->len;

		if (copy <= 0 || !tcp_skb_can_collapse_to(skb)) {
			bool first_skb;

		new_segment:
			if (!sk_stream_memory_free(sk))
				goto wait_for_space;

			if (unlikely(process_backlog >= 16)) {
				process_backlog = 0;
				if (sk_flush_backlog(sk))
					goto restart;
			}
			first_skb = tcp_rtx_and_write_queues_empty(sk);
			skb = sk_stream_alloc_skb(sk, 0, sk->sk_allocation, first_skb);
			if (!skb)
				goto wait_for_space;

			process_backlog++;
			skb->ip_summed = CHECKSUM_PARTIAL;

			tcp_skb_entail(sk, skb);
			copy = size_goal;

			/* All packets are restored as if they have
			 * already been sent. skb_mstamp_ns isn't set to
			 * avoid wrong rtt estimation.
			 */
			if (tp->repair)
				TCP_SKB_CB(skb)->sacked |= TCPCB_REPAIRED;
		}

		/* Try to append data to the end of skb. */
		if (copy > msg_data_left(msg))
			copy = msg_data_left(msg);

		/* Where to copy to? */
		if (skb_availroom(skb) > 0 && !zc) {
			/* We have some space in skb head. Superb! */
			copy = min_t(int, copy, skb_availroom(skb));
			err = skb_add_data_nocache_copyer(q, sk, skb, &msg->msg_iter, copy); // hjk: todo
			if (err)
				goto do_fault;
		} else if (!zc) {
			bool merge = true;
			int i = skb_shinfo(skb)->nr_frags;
			struct page_frag *pfrag = sk_page_frag(sk);

			if (!sk_page_frag_refill(sk, pfrag))
				goto wait_for_space;

			if (!skb_can_coalesce(skb, i, pfrag->page, pfrag->offset)) {
				if (i >= READ_ONCE(sysctl_max_skb_frags)) {
					tcp_mark_push(tp, skb);
					goto new_segment;
				}
				merge = false;
			}

			copy = min_t(int, copy, pfrag->size - pfrag->offset);

			if (!sk_wmem_schedule(sk, copy))
				goto wait_for_space;

			err = skb_copy_to_page_nocache_copyer(q, sk, &msg->msg_iter, skb, pfrag->page, pfrag->offset,
							      copy); // hjk: todo
			// printk("skb %lld,copy = %d pfrag->size = %d\n", (unsigned long long)skb, copy, pfrag->size);
			if (err)
				goto do_error;

			/* Update the skb. */
			if (merge) {
				skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy);
			} else {
				skb_fill_page_desc(skb, i, pfrag->page, pfrag->offset, copy);
				page_ref_inc(pfrag->page);
			}
			pfrag->offset += copy;
		}
		// else {
		// 	if (!sk_wmem_schedule(sk, copy))
		// 		goto wait_for_space;

		// 	err = skb_zerocopy_iter_stream(sk, skb, msg, copy, uarg);
		// 	if (err == -EMSGSIZE || err == -EEXIST) {
		// 		tcp_mark_push(tp, skb);
		// 		goto new_segment;
		// 	}
		// 	if (err < 0)
		// 		goto do_error;
		// 	copy = err;
		// }

		if (!copied)
			TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_PSH;

		WRITE_ONCE(tp->write_seq, tp->write_seq + copy);
		TCP_SKB_CB(skb)->end_seq += copy;
		tcp_skb_pcount_set(skb, 0);

		copied += copy;
		if (!msg_data_left(msg)) {
			if (unlikely(flags & MSG_EOR))
				TCP_SKB_CB(skb)->eor = 1;
			goto out;
		}

		if (skb->len < size_goal || (flags & MSG_OOB) || unlikely(tp->repair))
			continue;

		if (forced_push(tp)) {
			tcp_mark_push(tp, skb);
			__tcp_push_pending_frames_copyer(sk, mss_now, TCP_NAGLE_PUSH);
		} else if (skb == tcp_send_head(sk))
			tcp_push_one_copyer(sk, mss_now);
		continue;

	wait_for_space:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		if (copied)
			tcp_push(sk, flags & ~MSG_MORE, mss_now, TCP_NAGLE_PUSH, size_goal);

		err = sk_stream_wait_memory(sk, &timeo);
		if (err != 0)
			goto do_error;

		mss_now = tcp_send_mss(sk, &size_goal, flags);
	}

out:
	if (copied) {
		tcp_tx_timestamp(sk, sockc.tsflags);
		tcp_push_copyer(sk, flags, mss_now, tp->nonagle, size_goal);
	}
out_nopush:
	net_zcopy_put(uarg);
	return copied + copied_syn;

do_error:
	skb = tcp_write_queue_tail(sk);
do_fault:
	tcp_remove_empty_skb(sk, skb);

	if (copied + copied_syn)
		goto out;
out_err:
	net_zcopy_put_abort(uarg, true);
	err = sk_stream_error(sk, flags, err);
	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(tcp_rtx_and_write_queues_empty(sk) && err == -EAGAIN)) {
		sk->sk_write_space(sk);
		tcp_chrono_stop(sk, TCP_CHRONO_SNDBUF_LIMITED);
	}
	return err;
}

int tcp_sendmsg_copyer_lazy(struct sock *sk, struct msghdr *msg, size_t size, int lazy_fd)
{
	int ret;

	lock_sock(sk);
	ret = tcp_sendmsg_locked_copyer_lazy(sk, msg, size, lazy_fd);
	release_sock(sk);

	return ret;
}

int inet_sendmsg_copyer_lazy(struct socket *sock, struct msghdr *msg, size_t size, int lazy_fd)
{
	struct sock *sk = sock->sk;

	if (unlikely(inet_send_prepare(sk)))
		return -EAGAIN;

	return tcp_sendmsg_copyer_lazy(sk, msg, size, lazy_fd);
}

int __sys_sendto_copyer_lazy(int fd, void __user *buff, size_t len, unsigned int flags, struct sockaddr __user *addr, int addr_len, int lazy_fd)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err;
	struct msghdr msg;
	struct iovec iov;
	int fput_needed;

	err = import_single_range(WRITE, buff, len, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	if (addr) {
		err = move_addr_to_kernel(addr, addr_len, &address);
		if (err < 0)
			goto out_put;
		msg.msg_name = (struct sockaddr *)&address;
		msg.msg_namelen = addr_len;
	}
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags = flags;
	err = inet_sendmsg_copyer_lazy(sock, &msg, msg_data_left(&msg), lazy_fd);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE5(sendto_copyer_lazy, int, fd, void __user *, buff, size_t, len, unsigned int, flags, int, lazy_fd)
{
	return __sys_sendto_copyer_lazy(fd, buff, len, flags, NULL, 0, lazy_fd);
}

/**
 *	sock_sendmsg - send a message through @sock
 *	@sock: socket
 *	@msg: message to send
 *
 *	Sends @msg through @sock, passing through LSM.
 *	Returns the number of bytes sent, or an error code.
 */
int COPYER_WRAP(sock_sendmsg)(struct socket *sock, struct msghdr *msg)
{
	int err = security_socket_sendmsg(sock, msg, msg_data_left(msg));

	return err ?: inet_sendmsg(sock, msg, msg_data_left(msg));
}
EXPORT_SYMBOL(sock_sendmsg_copyer);
