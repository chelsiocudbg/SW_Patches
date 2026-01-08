/*
 * Copyright (c) 2021-2025 Chelsio Communications. All rights reserved.
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
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
#ifndef __CSTOR_UDEFS_H__
#define __CSTOR_UDEFS_H__

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>
#include <linux/types.h>
#include <util/compiler.h>
#include <util/udma_barrier.h>
#include <endian.h>
#include "libcstor.h"
#include "cstor_umain.h"
#include "cstor_ioctl.h"

#define ROUND_UP(x, n) (((x) + (n) - 1u) & ~((n) - 1u))
#define ROUND_DOWN(x, n) ((x) & ~((n) - 1u))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define IS_POWER_OF_2(x) ((x) && (!((x) & ((x) - 1))))

/* FIXME: Move me to a generic PCI mmio accessor */
#define cpu_to_pci32(val) htole32(val)

#define writel(v, a) do { *((volatile u32 *)(a)) = cpu_to_pci32(v); } while (0)

#include "t4_regs.h"
#include "t4_chip_type.h"
#include "t4fw_interface.h"
#include "t4_msg.h"
#include "t4_hw.h"
#include "t4_regs_values.h"

#define LIBNAME "libcstor"

enum {
	CSTOR_NOLOG	= 0,
	CSTOR_LOG	= 1
};

#ifdef DEBUG
#define cstor_debug(ucdev, log, fmt, ...)						\
	do {										\
		if (ucdev) {								\
			fprintf(stdout, LIBNAME ": %s: %s: %d: %s: " fmt,		\
				(ucdev)->cdev.name,					\
				__FILE__, __LINE__, __func__, ##__VA_ARGS__);		\
			if (log)							\
				syslog(LOG_DEBUG, LIBNAME ": %s: %s: %d: %s: " fmt,	\
					(ucdev)->cdev.name,				\
					__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
		} else	{								\
			fprintf(stdout, LIBNAME ": %s: %d: %s: " fmt,			\
				__FILE__, __LINE__, __func__, ##__VA_ARGS__);		\
			if (log)							\
				syslog(LOG_DEBUG, LIBNAME ": %s: %d: %s: " fmt,		\
					__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
		}									\
	} while (0)
#else
#define cstor_debug(ucdev, fmt, ...) do {} while (0)
#endif

#define cstor_info(ucdev, log, fmt, ...)					\
	do {									\
		fprintf(stdout, LIBNAME ": %s: %s: %d: %s: " fmt,		\
			(ucdev)->cdev.name,					\
			__FILE__, __LINE__, __func__, ##__VA_ARGS__);		\
		if (log) {							\
			syslog(LOG_INFO, LIBNAME ": %s: %s: %d: %s: " fmt,	\
				(ucdev)->cdev.name,				\
				__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
		}								\
	} while (0)

#define cstor_err(ucdev, log, fmt, ...)						\
	do {									\
		fprintf(stderr, LIBNAME ": %s: %s: %d: %s: " fmt,		\
			(ucdev)->cdev.name,					\
			__FILE__, __LINE__, __func__, ##__VA_ARGS__);		\
		if (log) {							\
			syslog(LOG_ERR, LIBNAME ": %s: %s: %d: %s: " fmt,	\
				(ucdev)->cdev.name,				\
				__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
		}								\
	} while (0)

#define cstor_printf(stream, log, fmt, ...)					\
	do {									\
		fprintf(stream, LIBNAME ": %s: %d: %s: " fmt,			\
			__FILE__, __LINE__, __func__, ##__VA_ARGS__);		\
		if (log) {							\
			if (fileno(stream) == 1)					\
				syslog(LOG_INFO, LIBNAME ": %s: %d: %s: " fmt,		\
					__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
			else if (fileno(stream) == 2)					\
				syslog(LOG_ERR, LIBNAME ": %s: %d: %s: " fmt,		\
					__FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
		}									\
	} while (0)

#define T4_EQ_ENTRY_SIZE 64

#define T4_SQ_NUM_SLOTS 5
#define T4_SQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_SQ_NUM_SLOTS)
#define T4_MAX_SEND_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_v2_nvmet_tx_data_wr) - \
			 sizeof(struct fw_ri_isgl)) / sizeof(struct fw_ri_sge))
#define T4_MAX_SEND_IMM_DATA 255

#define T4_RQ_NUM_SLOTS 2
#define T4_MAX_RQ_SGE 4
#define T4_MAX_RXQ_SGE	1

union t4_wr {
	struct fw_v2_nvmet_tx_data_wr send_wr;
	struct fw_nvmet_v2_fr_nsmr_wr nsmr_wr;
	struct fw_ri_inv_lstag_wr inv_wr;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_SQ_NUM_SLOTS];
} __attribute__((aligned(T4_EQ_ENTRY_SIZE)));

union t4_recv_wr {
	struct fw_ri_recv_wr recv;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_RQ_NUM_SLOTS];
};

/*
 * 128B CQE entries.
 */
struct t4_cqe {
	union {
		struct cpl_nvmt_cmp_imm cmp;
		struct cpl_nvmt_cmp_srq scmp;
		struct cpl_rdma_cqe cqe;
		struct cpl_rdma_cqe_err cqe_err;
		__be64 flits[3];
	} u;
	__be64 reserved[12];
	__be64 bits_type_ts;
};

#define S_CQE_GENBIT	63
#define M_CQE_GENBIT	0x1
#define G_CQE_GENBIT(x)	(((x) >> S_CQE_GENBIT) & M_CQE_GENBIT)
#define V_CQE_GENBIT(x) ((x) << S_CQE_GENBIT)

#define CQE_GENBIT(x)	((unsigned int)G_CQE_GENBIT(be64toh((x)->bits_type_ts)))

struct t4_swsqe {
	void *ctx;
};

struct t4_sq {
	union t4_wr *queue;
	struct t4_swsqe *sw_sq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	bool wc_reg_available;
};

struct t4_swrqe {
	void *ctx;
	bool valid;
};

struct t4_rq {
	union  t4_recv_wr *queue;
	struct t4_swrqe *sw_rq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u16 in_use;
	u16 max_wr;
	u16 size;
	u16 wr_cidx;
	u16 wr_pidx;
	u16 pidx;
	bool wc_reg_available;
};

struct t4_wq {
	struct t4_sq sq;
	struct t4_rq rq;
	u32 qid_mask;
	u8 *qp_errp;
};

static inline int t4_rq_empty(struct t4_wq *wq)
{
	return !wq->rq.in_use;
}

static inline u32 t4_rq_avail(struct t4_wq *wq)
{
	return wq->rq.max_wr - 1 - wq->rq.in_use;
}

static inline void t4_rq_produce(struct t4_wq *wq, u8 ndesc)
{
	wq->rq.in_use++;
	if (++wq->rq.wr_pidx == wq->rq.max_wr)
		wq->rq.wr_pidx = 0;

	wq->rq.pidx += ndesc;
	if (wq->rq.pidx >= wq->rq.size)
		wq->rq.pidx %= wq->rq.size;
}

static inline void t4_rq_consume(struct t4_wq *wq)
{
	wq->rq.in_use--;
	if (++wq->rq.wr_cidx == wq->rq.max_wr)
		wq->rq.wr_cidx = 0;

	assert((wq->rq.wr_cidx != wq->rq.wr_pidx) || !wq->rq.in_use);
}

struct t4_srq {
	union  t4_recv_wr *queue;
	struct t4_swrqe *sw_rq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u32 rqt_abs_idx;
	u16 in_use;
	u16 max_wr;
	u16 size;
	u16 wr_cidx;
	u16 wr_pidx;
	u16 pidx;
	bool wc_reg_available;
};

static inline u32 t4_srq_avail(struct t4_srq *srq)
{
	return srq->max_wr - 1 - srq->in_use;
}

static inline void t4_srq_produce(struct t4_srq *srq, u8 ndesc)
{
	srq->in_use++;
	assert(srq->in_use < srq->max_wr);
	if (++srq->wr_pidx == srq->max_wr)
		srq->wr_pidx = 0;

	assert(srq->wr_cidx != srq->wr_pidx); /* overflow */
	srq->pidx += ndesc;
	if (srq->pidx >= srq->size)
		srq->pidx %= srq->size;
}

static inline void t4_srq_consume(struct t4_srq *srq)
{
	assert(srq->in_use > 0);
	srq->in_use--;
	if (++srq->wr_cidx == srq->max_wr)
		srq->wr_cidx = 0;

	assert((srq->wr_cidx != srq->wr_pidx) || srq->in_use == 0);
}

static inline void copy_wqe_to_udb(volatile u32 *udb_offset, void *wqe)
{
	u64 *src = (u64 *)wqe;
	u64 *dst = (u64 *)udb_offset;
	u8 len16 = 4;

	while (len16) {
		*dst++ = *src++;
		*dst++ = *src++;
		len16--;
	}
}

static inline void t4_ring_srq_db(struct t4_srq *srq, u16 inc, u8 len16, union t4_recv_wr *wqe,
				  u8 plat_dev)
{
	mmio_wc_start();

	if (plat_dev)
		writel(V_QID(srq->qid) | V_ARM_QTYPE(0) | V_ARM_PIDX(inc), srq->udb);
	else
		writel(V_QID(srq->bar2_qid) | V_PIDX_T5(inc), srq->udb);

	mmio_flush_writes();
}

static inline u32 t4_sq_avail(struct t4_wq *wq)
{
	return wq->sq.size - 1 - wq->sq.in_use;
}

static inline void t4_sq_produce(struct t4_wq *wq, u16 ndesc)
{
	wq->sq.in_use += ndesc;
	wq->sq.pidx += ndesc;

	if (wq->sq.pidx >= wq->sq.size)
		wq->sq.pidx -= wq->sq.size;
}

static inline void t4_sq_consume(struct t4_wq *wq, u16 last_cidx)
{
	u16 cidx = last_cidx + 1;
	int claimable = cidx - wq->sq.cidx;

	if (claimable < 0)
		claimable += wq->sq.size;

	assert(wq->sq.in_use >= claimable);

	wq->sq.in_use -= claimable;
	wq->sq.cidx = (cidx == wq->sq.size) ? 0 : cidx;
}

static inline void
t4_ring_sq_db(struct t4_wq *wq, u16 inc, u8 len16, union t4_wr *wqe, bool wc_enabled, u8 plat_dev)
{
	mmio_wc_start();

	if (wc_enabled && (inc == 1) && wq->sq.wc_reg_available) {
		copy_wqe_to_udb(wq->sq.udb + 14, wqe);
	} else {
		if (plat_dev)
			writel(V_QID(wq->sq.qid) | V_ARM_QTYPE(0) | V_ARM_PIDX(inc), wq->sq.udb);
		else
			writel(V_QID(wq->sq.bar2_qid) | V_PIDX_T5(inc), wq->sq.udb);
	}

	mmio_flush_writes();
}

static inline void t4_ring_rq_db(struct t4_wq *wq, u16 inc, u8 len16, union t4_recv_wr *wqe,
				 u8 plat_dev)
{
	mmio_wc_start();

	if (plat_dev)
		writel(V_QID(wq->rq.qid) | V_ARM_QTYPE(0) | V_ARM_PIDX(inc), wq->rq.udb);
	else
		writel(V_QID(wq->rq.bar2_qid) | V_PIDX_T5(inc), wq->rq.udb);

	mmio_flush_writes();
}

static inline int t4_wq_in_error(struct t4_wq *wq)
{
	return *wq->qp_errp;
}

struct t4_cq {
	struct t4_cqe *queue;
	volatile u32 *ugts;
	size_t memsize;
	u32 cqid;
	u32 qid_mask;
	u16 size;	/* excluding status page */
	u16 cidx;
	u16 cidx_inc;
	u16 max_cidx_inc;
	u8 gen;
};

static inline int t4_valid_cqe(struct t4_cq *q, struct t4_cqe *cqe)
{
	return (CQE_GENBIT(cqe) == q->gen);
}

static inline int t4_cq_notempty(struct t4_cq *q)
{
	return t4_valid_cqe(q, &q->queue[q->cidx]);
}

static inline int t4_cq_armed(struct t4_cq *q)
{
	return ((struct t4_status_page *)&q->queue[q->size])->cq_armed;
}

static inline void t4_cq_arm(struct t4_cq *q)
{
	((struct t4_status_page *)&q->queue[q->size])->cq_armed = 1;
}

static inline int t4_arm_cq(struct t4_cq *q, int se, u8 plat_dev)
{
	u32 val;
	u16 max_cidx_inc = plat_dev ? M_ARM_CIDXINC : M_CIDXINC;

	if (t4_cq_armed(q) && !q->cidx_inc)
		return 0;

	t4_cq_arm(q);
	while (q->cidx_inc > max_cidx_inc) {
		if (plat_dev)
			val = V_INGRESSQID(q->cqid) | V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_SEINTARM(0) | V_ARM_QTYPE(1) | V_ARM_CIDXINC(max_cidx_inc);
		else
			val = V_INGRESSQID(q->cqid & q->qid_mask) |
			      V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_SEINTARM(0) | V_CIDXINC(max_cidx_inc);

		mmio_wc_start();
		writel(val, q->ugts);
		mmio_flush_writes();

		q->cidx_inc -= max_cidx_inc;
	}

	if (plat_dev)
		val = V_INGRESSQID(q->cqid) | V_TIMERREG(X_TIMERREG_RESTART_COUNTER) |
		      V_SEINTARM(se) | V_ARM_QTYPE(1) | V_ARM_CIDXINC(q->cidx_inc);
	else
		val = V_INGRESSQID(q->cqid & q->qid_mask) |
		      V_TIMERREG(X_TIMERREG_RESTART_COUNTER) |
		      V_SEINTARM(se) | V_CIDXINC(q->cidx_inc);

	mmio_wc_start();
	writel(val, q->ugts);
	mmio_flush_writes();

	q->cidx_inc = 0;
	return 0;
}

static inline void t4_cq_consume(struct t4_cq *q, u8 plat_dev)
{
	if (++q->cidx_inc == q->max_cidx_inc) {
		u32 val;

		if (plat_dev)
			val = V_INGRESSQID(q->cqid) | V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_SEINTARM(0) | V_ARM_QTYPE(1) | V_ARM_CIDXINC(q->cidx_inc);
		else
			val = V_INGRESSQID(q->cqid & q->qid_mask) |
			      V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_SEINTARM(0) | V_CIDXINC(q->cidx_inc);

		mmio_wc_start();
		writel(val, q->ugts);
		mmio_flush_writes();

		q->cidx_inc = 0;
	}

	if (++q->cidx == q->size) {
		q->cidx = 0;
		q->gen ^= 1;
	}
}

static inline int t4_next_cqe(struct t4_cq *q, struct t4_cqe **cqe)
{
	if (!t4_valid_cqe(q, &q->queue[q->cidx]))
		return ENODATA;

	udma_from_device_barrier();
	*cqe = &q->queue[q->cidx];

	return 0;
}

struct t4_iqe {
	struct rss_header rss_hdr;	/* flit 0 */
	__be64 reserved1;		/* flit 1 */
	__be64 reserved2;		/* flit 2 */
	__be64 reserved3;		/* flit 3 */
	__be64 reserved4;		/* flit 4 */
	__be64 reserved5;		/* flit 5 */
	__be64 newbuf_dma_len;		/* flit 6 */
	__be64 bits_type_ts;		/* flit 7 */
};

struct t4_iq {
	struct t4_iqe *queue;
	volatile u32 *gts;
	size_t memsize;
	u32 bar2_qid;
	u16 size;
	u16 cidx;
	u16 cidx_inc;
	u16 qid;
	u16 abs_id;
	u16 max_cidx_inc;
	u16 iqe_len;
	u8 gen;
};

struct fl_desc {
	void *ctx;
	u64 addr;
};

struct t4_fl {
	u64 *queue;
	struct fl_desc *sw_queue;
	volatile u32 *db;
	size_t memsize;
	u32 bar2_qid;
	u32 fl_align;
	u32 offset;
	u32 fl_page_size;
	u16 qid;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 in_use;
	u16 pend_cred;
};

static inline void t4_fl_consume(struct t4_fl *fl)
{
	fl->in_use--;
	if (++fl->cidx == fl->size)
		fl->cidx = 0;

	assert((fl->cidx != fl->pidx) || !fl->in_use);
}

static inline u16 t4_fl_avail(struct t4_fl *fl)
{
	return fl->size ? fl->size - 1 - fl->in_use : 0;
}

static inline void t4_fl_produce(struct t4_fl *fl)
{
	fl->in_use++;
	if (++fl->pidx == fl->size)
		fl->pidx = 0;
}

static inline void t4_ring_fl_db(struct t4_fl *fl, u8 plat_dev)
{
	if (fl->pend_cred >= 8) {
		mmio_wc_start();

		if (plat_dev)
			writel(V_QID(fl->qid) | V_ARM_QTYPE(0) | V_ARM_PIDX(fl->pend_cred / 8),
			       fl->db);
		else
			writel(V_QID(fl->bar2_qid) | V_PIDX_T5(fl->pend_cred / 8), fl->db);

		mmio_flush_writes();

		fl->pend_cred &= 7;
	}
}

static inline void t4_iq_consume(struct t4_iq *iq, u8 plat_dev)
{
	if (++iq->cidx_inc == iq->max_cidx_inc) {
		u32 val;

		if (plat_dev)
			val = V_INGRESSQID(iq->qid) | V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_ARM_QTYPE(1) | V_ARM_CIDXINC(iq->cidx_inc);
		else
			val = V_INGRESSQID(iq->bar2_qid) | V_TIMERREG(X_TIMERREG_UPDATE_CIDX) |
			      V_CIDXINC(iq->cidx_inc);

		mmio_wc_start();
		writel(val, iq->gts);
		mmio_flush_writes();

		iq->cidx_inc = 0;
	}

	if (++iq->cidx == iq->size) {
		iq->cidx = 0;
		iq->gen ^= 1;
	}
}

static inline bool is_new_response(struct t4_iq *iq)
{
	const struct rsp_ctrl *rc = ((void *)&iq->queue[iq->cidx]) + (iq->iqe_len - sizeof(*rc));

	return ((rc->u.type_gen >> S_RSPD_GEN) == iq->gen);
}

static inline int t4_next_iqe(struct t4_iq *iq, struct t4_iqe **iqe)
{
	if (!is_new_response(iq))
		return ENODATA;

	udma_from_device_barrier();
	*iqe = &iq->queue[iq->cidx];

	return 0;
}
#endif
