/*
 * Copyright (c) 2006-2021 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef IWCH_H
#define IWCH_H

#include <pthread.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <infiniband/driver.h>
#include <util/udma_barrier.h>
#include <ccan/list.h>
#include "t4.h"
#include <netinet/ip.h>
#include <netinet/udp.h>

extern unsigned long chrd_page_size;
extern unsigned long chrd_page_mask;
extern unsigned long chrd_page_shift;

struct chrd_mr;

struct chrd_dev {
	struct verbs_device ibv_dev;
	unsigned chip_version;
	int max_mr;
	struct chrd_mr **mmid2ptr;
	int max_qp;
	struct chrd_qp **qpid2ptr;
	int max_cq;
	struct chrd_cq **cqid2ptr;
	struct chrd_raw_qp **fid2ptr;
	struct list_head srq_list;
	pthread_spinlock_t lock;
	struct list_node list;
	int abi_version;
	int nfids;
	int fid_base;
	int nhpfids;
	bool write_cmpl_supported;
};

static inline int dev_is_t7(struct chrd_dev *dev)
{
	return dev->chip_version == CHELSIO_T7;
}

static inline int dev_is_t6(struct chrd_dev *dev)
{
	return dev->chip_version == CHELSIO_T6;
}

static inline int dev_is_t5(struct chrd_dev *dev)
{
	return dev->chip_version == CHELSIO_T5;
}

static inline int dev_is_t4(struct chrd_dev *dev)
{
	return dev->chip_version == CHELSIO_T4;
}

struct chrd_context {
	struct ibv_context ibv_ctx;
	struct t4_dev_status_page *status_page;
	int status_page_size;
};

struct chrd_pd {
	struct ibv_pd ibv_pd;
};

struct chrd_mr {
	struct ibv_mr ibv_mr;
	uint64_t va_fbo;
	uint64_t len;
	unsigned long page_size;
	unsigned long page_shift;
	unsigned long page_mask;
	uint64_t sw_pbl[];
};

static inline u32 chrd_mmid(u32 stag)
{
	return (stag >> 8);
}

struct chrd_cq {
	struct ibv_cq ibv_cq;
	struct chrd_dev *rhp;
	struct t4_cq cq;
	pthread_spinlock_t lock;
	struct t4_iq *iq;
#ifdef STALL_DETECTION
	struct timeval time;
#endif
};

struct chrd_vlan {
	__be16  tag;
	__be16  type;
};

struct chrd_deth {
	__be32       qkey;
	__be32       source_qpn;
};

struct chrd_bth_hdr {
        u8                      opcode;
        u8                      flags;
        __be16                  pkey;
        __be32                  destination_qpn;
        __be32                  apsn;
};

struct chrd_ah {
	struct ibv_ah ibah;
	struct chrd_dev *rhp;
	struct chrd_pd *php;
	struct chrd_wr_wait *wr_waitp;

	/* AV */
	bool ipv4:1;
	bool insert_vlan_tag:1;
	u8 smac[6];
	u8 dmac[6];
	u16 src_port;
	u16 dst_port;
	u32 local_ip_addr[4];
	u32 dest_ip_addr[4];
	u32 mtu;
	u32 flowlabel;
	u16 p_key;
	u32 dest_qp;
	u8 gid_index;
	u8 stat_rate;
	u8 hop_limit;
	u8 net_type;
	u16 vlan_id;
	u8 vlan_en;
	u8 tclass;
	u8 port;
	u8 sl;

	/* HW queues */
	u16 ctrlq_idx;
	u16 rss_qid;
	u16 txq_idx;

	/* add id for each ah */
	int ah_id;
};

struct chrd_gsi_attr {
	u8 ttl;
	u8 tos;
	u32 snd_mss;
	u16 vlan_tag;
	u16 arp_idx;
	u32 flow_label;
	u8 udp_state;
	u32 psn_nxt;
	u32 lsn;
	u32 epsn;
	u32 psn_max;
	u32 psn_una;
	u32 cwnd;
	u32 pf;
	u8 rexmit_thresh;
	u8 rnr_nak_thresh;
};

struct chrd_roce_qp_attributes {
	u32 q_key;
	u16 err_rq_idx;
	u8 roce_tver;
	u8 ack_credits;
	u8 err_rq_idx_valid;
	u32 pd_id;
	u16 ord_size;
	u16 ird_size;
	u32 hwtid;
	u32 atid;
	u8 port;
	struct chrd_ah roce_ah;
	struct chrd_gsi_attr gsi_attr;
};

enum qp_transport_type {
	CHRD_TRANSPORT_IWARP,
	CHRD_TRANSPORT_ROCEV2,
};

struct chrd_qp {
	struct ibv_qp ibv_qp;
	struct chrd_dev *rhp;
	struct t4_wq wq;
	pthread_spinlock_t lock;
	int sq_sig_all;
	struct chrd_srq *srq;
	enum qp_transport_type qp_trans;
	struct chrd_roce_qp_attributes roce_attr;
};

#define to_chrd_xxx(xxx, type)						\
	((struct chrd_##type *)						\
	 ((void *) ib##xxx - offsetof(struct chrd_##type, ibv_##xxx)))

struct chrd_raw_srq;

struct chrd_raw_qp {
	struct ibv_qp ibv_qp;
	struct chrd_cq *rcq;
	struct chrd_cq *scq;
	struct chrd_dev *rhp;
	struct t4_iq iq;
	struct t4_txq txq;
	struct t4_raw_fl fl;
	pthread_spinlock_t lock;
	int sq_sig_all;
	uint32_t fid;
	int qid_mask;
	struct chrd_raw_srq *srq;
};

enum {
	C4IW_SRQ_RAW,
	C4IW_SRQ_BASIC,
};

enum chrd_v2_ing_cqe_opcode {
	IB_CQE_V2_OPC_SEND_FIRST,
	IB_CQE_V2_OPC_SEND_MIDDLE,
	IB_CQE_V2_OPC_SEND_LAST,
	IB_CQE_V2_OPC_SEND_LAST_WITH_IMM,
	IB_CQE_V2_OPC_SEND_ONLY,
	IB_CQE_V2_OPC_SEND_ONLY_WITH_IMM,
	IB_CQE_V2_OPC_WRITE_FIRST,
	IB_CQE_V2_OPC_WRITE_MIDDLE,
	IB_CQE_V2_OPC_WRITE_LAST,
	IB_CQE_V2_OPC_WRITE_LAST_WITH_IMM,
	IB_CQE_V2_OPC_WRITE_ONLY,
	IB_CQE_V2_OPC_WRITE_ONLY_WITH_IMM,
	IB_CQE_V2_OPC_READ_REQUEST,
	IB_CQE_V2_OPC_READ_RESPONSE_FIRST,
	IB_CQE_V2_OPC_READ_RESPONSE_MIDDLE,
	IB_CQE_V2_OPC_READ_RESPONSE_LAST,
	IB_CQE_V2_OPC_READ_RESPONSE_ONLY,
	IB_CQE_V2_OPC_ACK,
	IB_CQE_V2_OPC_SEND_LAST_WITH_INV = 0x16,
	IB_CQE_V2_OPC_SEND_ONLY_WITH_INV = 0x17,
};

static inline int v2_ib_opc_to_fw_opc(enum chrd_v2_ing_cqe_opcode opcode)
{
	switch (opcode) {
	case IB_CQE_V2_OPC_SEND_FIRST:
	case IB_CQE_V2_OPC_SEND_MIDDLE:
	case IB_CQE_V2_OPC_SEND_LAST:
	case IB_CQE_V2_OPC_SEND_ONLY:
		return FW_RI_SEND;
	case IB_CQE_V2_OPC_SEND_LAST_WITH_INV:
	case IB_CQE_V2_OPC_SEND_ONLY_WITH_INV:
		return FW_RI_SEND_WITH_INV;
#if 0 //Bhar: enable send_imm when fw enables it
	case IB_CQE_V2_OPC_SEND_LAST_WITH_IMM:
	case IB_CQE_V2_OPC_SEND_ONLY_WITH_IMM:
		return FW_RI_SEND_IMMEDIATE;
#endif
	case IB_CQE_V2_OPC_WRITE_FIRST:
	case IB_CQE_V2_OPC_WRITE_MIDDLE:
	case IB_CQE_V2_OPC_WRITE_LAST:
	case IB_CQE_V2_OPC_WRITE_ONLY:
		return FW_RI_RDMA_WRITE;
	case IB_CQE_V2_OPC_WRITE_LAST_WITH_IMM:
	case IB_CQE_V2_OPC_WRITE_ONLY_WITH_IMM:
		return FW_RI_WRITE_IMMEDIATE;
	case IB_CQE_V2_OPC_READ_REQUEST:
		return FW_RI_READ_REQ;
	case IB_CQE_V2_OPC_READ_RESPONSE_FIRST:
	case IB_CQE_V2_OPC_READ_RESPONSE_MIDDLE:
	case IB_CQE_V2_OPC_READ_RESPONSE_LAST:
	case IB_CQE_V2_OPC_READ_RESPONSE_ONLY:
		return FW_RI_READ_RESP;
	default:
		return 0x1F; //Bhar: setting opc to reserved code to deal with it in poll_cq_one()
	}
}

struct chrd_raw_srq {
	struct ibv_srq ibv_srq;
	int type;			/* must be 2nd in this struct */
	struct chrd_dev *rhp;
	struct t4_iq iq;
	struct t4_raw_fl fl;
	pthread_spinlock_t lock;
	int qid_mask;
};

struct chrd_srq {
	struct ibv_srq ibv_srq;
	int type;			/* must be 2nd in this struct */
	struct chrd_dev *rhp;
	struct t4_srq wq;
	struct list_node list;
	pthread_spinlock_t lock;
	uint32_t srq_limit;
	int armed;
	__u32 flags;
};

static inline struct chrd_srq *to_chrd_srq(struct ibv_srq *ibsrq)
{
	return to_chrd_xxx(srq, srq);
}

static inline struct chrd_raw_srq *iq_to_raw_srq(struct t4_iq *iq)
{
	return (struct chrd_raw_srq *)((void *)iq -
				      offsetof(struct chrd_raw_srq, iq));
}

static inline struct chrd_raw_srq *to_chrd_raw_srq(struct ibv_srq *ibsrq)
{
	return to_chrd_xxx(srq, raw_srq);
}

static inline struct chrd_raw_qp *get_raw_qp(struct chrd_dev *rhp, u32 qid)
{
	return (struct chrd_raw_qp *)rhp->qpid2ptr[qid];
}

static inline struct chrd_raw_qp *iq_to_raw_qp(struct t4_iq *iq)
{
	return (struct chrd_raw_qp *)((void *)iq -
				      offsetof(struct chrd_raw_qp, iq));
}

static inline struct chrd_dev *to_chrd_dev(struct ibv_device *ibdev)
{
	return to_chrd_xxx(dev, dev);
}

static inline struct chrd_context *to_chrd_context(struct ibv_context *ibctx)
{
	return to_chrd_xxx(ctx, context);
}

static inline struct chrd_pd *to_chrd_pd(struct ibv_pd *ibpd)
{
	return to_chrd_xxx(pd, pd);
}

static inline struct chrd_cq *to_chrd_cq(struct ibv_cq *ibcq)
{
	return to_chrd_xxx(cq, cq);
}

static inline struct chrd_qp *to_chrd_qp(struct ibv_qp *ibqp)
{
	return to_chrd_xxx(qp, qp);
}

static inline struct chrd_raw_qp *to_chrd_raw_qp(struct ibv_qp *ibqp)
{
	return to_chrd_xxx(qp, raw_qp);
}

static inline struct chrd_mr *to_chrd_mr(struct ibv_mr *ibmr)
{
	return to_chrd_xxx(mr, mr);
}

static inline struct chrd_qp *get_qhp(struct chrd_dev *rhp, u32 qid)
{
	return rhp->qpid2ptr[qid];
}

static inline struct chrd_cq *get_chp(struct chrd_dev *rhp, u32 qid)
{
	return rhp->cqid2ptr[qid];
}

static inline struct chrd_ah *to_chrd_ah(struct ibv_ah *ibah)
{
	return container_of(ibah, struct chrd_ah, ibah);
}

static inline unsigned long_log2(unsigned long x)
{
	unsigned r = 0;
	for (x >>= 1; x > 0; x >>= 1)
		r++;
	return r;
}

#ifdef HAVE_IBV_ENUMS_IN_API
#define ENUM_IBV_ACCESS_FLAGS enum ibv_access_flags
#define ENUM_IBV_SRQ_ATTR_MASK enum ibv_srq_attr_mask
#define ENUM_IBV_QP_ATTR_MASK enum ibv_qp_attr_mask
#else
#define ENUM_IBV_ACCESS_FLAGS int
#define ENUM_IBV_SRQ_ATTR_MASK int
#define ENUM_IBV_QP_ATTR_MASK int
#endif

int chrd_query_device(struct ibv_context *context,
			     struct ibv_device_attr *attr);
int chrd_query_port(struct ibv_context *context, uint8_t port,
			   struct ibv_port_attr *attr);

struct ibv_pd *chrd_alloc_pd(struct ibv_context *context);
int chrd_free_pd(struct ibv_pd *pd);

struct ibv_mr *chrd_reg_mr(struct ibv_pd *pd, void *addr,
				  size_t length, ENUM_IBV_ACCESS_FLAGS access);
int chrd_dereg_mr(struct ibv_mr *mr);

struct ibv_cq *chrd_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector);
int chrd_resize_cq(struct ibv_cq *cq, int cqe);
int chrd_destroy_cq(struct ibv_cq *cq);
int chrd_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc);
int chrd_arm_cq(struct ibv_cq *cq, int solicited);
void chrd_cq_event(struct ibv_cq *cq);
void chrd_init_cq_buf(struct chrd_cq *cq, int nent);

struct ibv_srq *chrd_create_srq(struct ibv_pd *pd,
				struct ibv_srq_init_attr *attr);
int chrd_modify_srq(struct ibv_srq *srq,
		    struct ibv_srq_attr *attr,
		    ENUM_IBV_SRQ_ATTR_MASK mask);
int chrd_destroy_srq(struct ibv_srq *srq);
int chrd_post_srq_recv(struct ibv_srq *ibsrq,
		       struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr);
int chrd_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr);
struct ibv_qp *chrd_create_qp(struct ibv_pd *pd,
			      struct ibv_qp_init_attr *attr);
int chrd_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   ENUM_IBV_QP_ATTR_MASK attr_mask);
int chrd_destroy_qp(struct ibv_qp *qp);
int chrd_query_qp(struct ibv_qp *qp,
		  struct ibv_qp_attr *attr,
		  ENUM_IBV_QP_ATTR_MASK attr_mask,
		  struct ibv_qp_init_attr *init_attr);
void chrd_flush_qp(struct chrd_qp *qhp);
void chrd_flush_qps(struct chrd_dev *dev);
int chrd_iw_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
		      struct ibv_send_wr **bad_wr);
int chrd_roce_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			struct ibv_send_wr **bad_wr);
int chrd_post_receive(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		      struct ibv_recv_wr **bad_wr);
struct ibv_ah *chrd_roce_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int chrd_roce_destroy_ah(struct ibv_ah *ibah);
struct ibv_ah *chrd_iw_create_ah(struct ibv_pd *pd,
			         struct ibv_ah_attr *ah_attr);
int chrd_iw_destroy_ah(struct ibv_ah *ibah);
int chrd_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
		      uint16_t lid);
int chrd_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
		      uint16_t lid);
void chrd_async_event(struct ibv_async_event *event);
void chrd_flush_hw_cq(struct chrd_cq *chp);
int chrd_flush_rq(struct chrd_qp *qhp, struct t4_cq *cq, int count);
void chrd_flush_sq(struct chrd_qp *qhp);
void chrd_count_rcqes(struct t4_cq *cq, struct t4_wq *wq, int *count, enum qp_transport_type prot);
void chrd_copy_wr_to_srq(struct t4_srq *srq, union t4_recv_wr *wqe, u8 len16);
void chrd_flush_srqidx(struct chrd_qp *qhp, u32 srqidx);

#define FW_MAJ 0
#define FW_MIN 0

static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

#ifdef STATS

#define INC_STAT(a) { chrd_stats.a++; }

struct chrd_stats {
	unsigned long send;
	unsigned long recv;
	unsigned long read;
	unsigned long write;
	unsigned long arm;
	unsigned long cqe;
	unsigned long mr;
	unsigned long qp;
	unsigned long cq;
};

extern struct chrd_stats chrd_stats;
#else
#define INC_STAT(a)
#endif

#ifndef IBV_QPT_RAW_ETH
#define IBV_QPT_RAW_ETH 8
#endif

#ifndef IBV_SEND_IP_CSUM
#define IBV_SEND_IP_CSUM (1 << 4)
#endif

#ifndef IBV_SEND_IP6_CSUM
#define IBV_SEND_IP6_CSUM (1 << 5)
#endif

#ifdef STALL_DETECTION
void dump_state(void);
extern int stall_to;
extern int dumped;
#endif

#ifndef uninitialized_var
#define uninitialized_var(x) x = x
#endif

#endif				/* IWCH_H */
