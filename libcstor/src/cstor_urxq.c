/*
 * Copyright (c) 2025 Chelsio Communications. All rights reserved.
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
 *
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

#include <stdio.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "cstor_umain.h"

enum cstor_iscsi_status {
	/* internal errors */
	CSTOR_ISCSI_RX_DATA_ERR = 0,
	CSTOR_ISCSI_TCP_SEQ_MISMATCH_ERR = 1,

	/* Actual iscsi errors */
	CSTOR_ISCSI_PAYLOAD_T10_ERR = 14,
	CSTOR_ISCSI_PPOD_MISMATCH_ERR = 15,
	CSTOR_ISCSI_DATA_DDP = 16,
	CSTOR_ISCSI_INVALID_LLIMIT_ERR = 17,
	CSTOR_ISCSI_PPOD_PARITY_ERR = 18,
	CSTOR_ISCSI_HDGST_ERR = 20,
	CSTOR_ISCSI_DDGST_ERR = 21,
	CSTOR_ISCSI_INVALID_TAG_ERR = 22,
	CSTOR_ISCSI_INVALID_ULIMIT_ERR = 23,
	CSTOR_ISCSI_INVALID_OFFSET_ERR = 24,
	CSTOR_ISCSI_INVALID_COLOR_ERR = 25,
	CSTOR_ISCSI_INVALID_TID_ERR = 26,
	CSTOR_ISCSI_INVALID_PPOD_ERR = 27,

	CSTOR_ISCSI_MAX_STATUS = 32
};

static const char *cstor_iscsi_status[CSTOR_ISCSI_MAX_STATUS] = {
	[CSTOR_ISCSI_RX_DATA_ERR] = "rx data err",
	[CSTOR_ISCSI_TCP_SEQ_MISMATCH_ERR] = "tcp seq number mismatch err",
	[CSTOR_ISCSI_PAYLOAD_T10_ERR] = "payload t10 err",
	[CSTOR_ISCSI_PPOD_MISMATCH_ERR] = "ppod mismatch err",
	[CSTOR_ISCSI_INVALID_LLIMIT_ERR] = "invalid llimit err",
	[CSTOR_ISCSI_PPOD_PARITY_ERR] = "ppod parity_err",
	[CSTOR_ISCSI_HDGST_ERR] = "header dgst err",
	[CSTOR_ISCSI_DDGST_ERR] = "data dgst err",
	[CSTOR_ISCSI_INVALID_TAG_ERR] = "invalid tag err",
	[CSTOR_ISCSI_INVALID_ULIMIT_ERR] = "invalid ulimit err",
	[CSTOR_ISCSI_INVALID_OFFSET_ERR] = "invalid offset err",
	[CSTOR_ISCSI_INVALID_COLOR_ERR] = "invalid clr err",
	[CSTOR_ISCSI_INVALID_TID_ERR] = "invalid tid err",
	[CSTOR_ISCSI_INVALID_PPOD_ERR] = "invalid ppod err",
};

const char *cstor_get_iscsi_status_str(u8 idx)
{
	if (idx >= CSTOR_ISCSI_MAX_STATUS) {
		cstor_printf(stderr, CSTOR_NOLOG, "Invalid idx! idx: %u max: %u\n",
			     idx, CSTOR_ISCSI_MAX_STATUS - 1);
		errno = EINVAL;
		return NULL;
	}

	return cstor_iscsi_status[idx];
}

static int __cstor_destroy_rxq(struct cstor_udevice *ucdev, u32 rxqid)
{
	struct cstor_destroy_rxq_cmd cmd = {};
	int ret;

	cmd.rxqid = rxqid;
	cstor_debug(ucdev, CSTOR_NOLOG, "rxqid %u\n", cmd.rxqid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DESTROY_RXQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DESTROY_RXQ cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

struct cstor_rxq *cstor_create_rxq(struct cstor_device *cdev, struct cstor_rxq_attr *attr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_urxq *urxq;
	struct cstor_create_rxq_cmd cmd = {};
	int ret;

	urxq = calloc(1, sizeof(*urxq));
	if (!urxq) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate urxq\n");
		ret = ENOMEM;
		goto err1;
	}

	cmd.port_id = attr->port_id;
	cmd.max_wr = attr->max_wr;
	cmd.fl_page_size = attr->fl_page_size;

	cstor_debug(ucdev, CSTOR_NOLOG, "port_id %u max_wr %u fl_page_size %u\n",
		    cmd.port_id, cmd.max_wr, cmd.fl_page_size);

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_CREATE_RXQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_RXQ cmd, "
			  "ret %d\n", ret);
		goto err2;
	}

	urxq->rxq.rxqid = cmd.resp.iq_id;
	urxq->ucdev = ucdev;
	urxq->fl.size = cmd.resp.fl_size;
	urxq->fl.memsize = cmd.resp.fl_memsize;
	urxq->fl.qid = cmd.resp.fl_id;
	urxq->fl.fl_align = cmd.resp.fl_align;
	urxq->fl.fl_page_size = cmd.fl_page_size;
	urxq->iq.size = cmd.resp.iq_size;
	urxq->iq.memsize = cmd.resp.iq_memsize;
	urxq->iq.gen = 1;
	urxq->iq.qid = cmd.resp.iq_id;
	urxq->iq.abs_id = cmd.resp.abs_id;
	urxq->iq.max_cidx_inc = min(urxq->iq.size - urxq->fl.size,
				    ucdev->plat_dev ? M_ARM_CIDXINC : M_CIDXINC);
	urxq->iq.iqe_len = cmd.resp.iqe_len;
	urxq->qid_mask = cmd.resp.qid_mask;
	cstor_spin_init(&urxq->lock, attr->no_lock);

	urxq->iq.queue = mmap(NULL, urxq->iq.memsize, PROT_WRITE, MAP_SHARED,
			      cdev->dev_fd, cmd.resp.iq_key);
	if (urxq->iq.queue == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, urxq->iq.memsize %lu "
			  "cmd.resp.iq_key %llu\n", urxq->iq.memsize, cmd.resp.iq_key);
		goto err3;
	}

	cstor_debug(ucdev, CSTOR_LOG, "iq qid %u iq.size %u, iq.memsize %lu\n",
		    urxq->iq.qid, urxq->iq.size, urxq->iq.memsize);

	urxq->fl.queue = mmap(NULL, urxq->fl.memsize, PROT_WRITE, MAP_SHARED,
			      cdev->dev_fd, cmd.resp.fl_key);
	if (urxq->fl.queue == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, urxq->fl.memsize %lu "
			  "cmd.resp.fl_key %llu\n", urxq->fl.memsize, cmd.resp.fl_key);
		goto err4;
	}

	cstor_debug(ucdev, CSTOR_LOG, "fl qid %u fl.size %u, fl.memsize %lu\n",
		    urxq->fl.qid, urxq->fl.size, urxq->fl.memsize);

	urxq->fl.db = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
			   cdev->dev_fd, cmd.resp.fl_db_key);
	if (urxq->fl.db == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
			  "cmd.resp.fl_db_key %llu\n", cstor_page_size, cmd.resp.fl_db_key);
		goto err5;
	}

	if (!ucdev->plat_dev) {
		urxq->fl.db += 2;
		urxq->fl.bar2_qid = urxq->fl.qid & urxq->qid_mask;
	}

	urxq->iq.gts = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
			    cdev->dev_fd, cmd.resp.iq_gts_key);
	if (urxq->iq.gts == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
			  "cmd.resp.iq_gts_key %llu\n", cstor_page_size, cmd.resp.iq_gts_key);
		goto err6;
	}

	if (!ucdev->plat_dev) {
		urxq->iq.gts += 3;
		urxq->iq.bar2_qid = urxq->iq.qid & urxq->qid_mask;
	}

	urxq->mapped = true;

	urxq->fl.sw_queue = calloc(urxq->fl.size, sizeof(struct fl_desc));
	if (!urxq->fl.sw_queue) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed urxq->fl.sw_queue allocation, "
			  "urxq->fl.size %u\n", urxq->fl.size);
		ret = ENOMEM;
		goto err7;
	}

	return &urxq->rxq;
err7:
	munmap(MASKED(urxq->iq.gts), cstor_page_size);
err6:
	munmap(MASKED(urxq->fl.db), cstor_page_size);
err5:
	munmap(urxq->fl.queue, urxq->fl.memsize);
err4:
	munmap(urxq->iq.queue, urxq->iq.memsize);
err3:
	__cstor_destroy_rxq(ucdev, urxq->iq.qid);
err2:
	free(urxq);
err1:
	errno = ret;
	return NULL;
}

int cstor_destroy_rxq(struct cstor_rxq *rxq)
{
	struct cstor_urxq *urxq = to_cstor_urxq(rxq);
	int ret;

	cstor_debug(urxq->ucdev, CSTOR_LOG, "iqid %u\n", urxq->iq.qid);

	if (urxq->mapped) {
		munmap(MASKED(urxq->fl.db), cstor_page_size);
		munmap(MASKED(urxq->iq.gts), cstor_page_size);
		munmap(urxq->fl.queue, urxq->fl.memsize);
		munmap(urxq->iq.queue, urxq->iq.memsize);
		urxq->mapped = false;
	}

	ret = __cstor_destroy_rxq(urxq->ucdev, urxq->iq.qid);
	if (ret) {
		cstor_err(urxq->ucdev, CSTOR_NOLOG, "__cstor_destroy_rxq() failed, qid %u\n",
			  urxq->iq.qid);
		return ret;
	}

	free(urxq->fl.sw_queue);
	free(urxq);

	return 0;
}

int
cstor_virt_to_dma_addr(struct cstor_udevice *ucdev, struct cstor_sge *sge,
		       u64 *dma_addr, u32 *length, bool *non_contiguous)
{
	struct cstor_umr *umr;
	u64 pg_off;
	u64 pg_id;
	u64 mr_off;

	pthread_spin_lock(&ucdev->lock);
	umr = ucdev->mmid2ptr[cstor_mmid(sge->lkey)];
	pthread_spin_unlock(&ucdev->lock);
	if (unlikely(!umr)) {
		cstor_err(ucdev, CSTOR_NOLOG, "umr unavailable, sge->lkey %u\n", sge->lkey);
		return EINVAL;
	}

	if (unlikely(sge->addr < umr->va_fbo)) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid sge->addr(%llu) < umr->va_fbo(%llu)\n",
			  sge->addr, umr->va_fbo);
		return EINVAL;
	}

	if (unlikely((sge->addr + sge->length) > (umr->va_fbo + umr->len))) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid (sge->addr(%llu) + sge->length(%u)) > "
			  "(umr->va_fbo(%llu) + umr->len(%llu))\n", sge->addr, sge->length,
			  umr->va_fbo, umr->len);
		return EINVAL;
	}

	pg_off = sge->addr & ~umr->page_mask;
	mr_off = sge->addr - umr->va_fbo;
	pg_id = ((umr->va_fbo & ~umr->page_mask) + mr_off) >> umr->page_shift;
	*dma_addr = umr->sw_pbl[pg_id] + pg_off;

	if ((pg_off + sge->length) > umr->page_size) {
		*length = umr->page_size - pg_off;
		*non_contiguous = 1;
	} else {
		*length = sge->length;
		*non_contiguous = 0;
	}

	return 0;
}

int
cstor_post_rxq_recv(struct cstor_rxq *rxq, struct cstor_recv_wr *wr, struct cstor_recv_wr **bad_wr)
{
	struct cstor_urxq *urxq = to_cstor_urxq(rxq);
	struct cstor_udevice *ucdev = urxq->ucdev;
	struct t4_fl *fl = &urxq->fl;
	u64 addr;
	u32 length;
	int ret;
	u16 num_wrs;
	u16 max_pidx = ucdev->plat_dev ? M_ARM_PIDX : M_PIDX_T5;
	bool non_contiguous;

	if (unlikely(!urxq->mapped)) {
		cstor_err(urxq->ucdev, CSTOR_NOLOG, "rxqid %u is not mapped\n", rxq->rxqid);
		return EINVAL;
	}

	cstor_spin_lock(&urxq->lock);
	num_wrs = t4_fl_avail(fl);
	while (wr) {
		if (unlikely(!num_wrs)) {
			cstor_debug(ucdev, CSTOR_NOLOG, "No slots! fl->pidx: %u fl->cidx: %u "
				    "fl->in_use: %u fl->size: %u fl->pend_cred: %u\n",
				    fl->pidx, fl->cidx, fl->in_use, fl->size, fl->pend_cred);
			*bad_wr = wr;
			ret = ENOMEM;
			break;
		}

		if (unlikely(wr->num_sge > T4_MAX_RXQ_SGE)) {
			cstor_err(ucdev, CSTOR_NOLOG, "invalid wr->num_sge %u > "
				  "T4_MAX_RXQ_SGE(%u)\n", wr->num_sge, T4_MAX_RXQ_SGE);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		if (unlikely(wr->sg_list->length != fl->fl_page_size)) {
			cstor_err(ucdev, CSTOR_NOLOG, "invalid wr->sg_list->length %u != "
				  "fl->fl_page_size %u\n", wr->sg_list->length, fl->fl_page_size);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		ret = cstor_virt_to_dma_addr(ucdev, wr->sg_list, &addr, &length, &non_contiguous);
		if (unlikely(ret || non_contiguous)) {
			cstor_err(ucdev, CSTOR_NOLOG, "cstor_virt_to_dma_addr() failed, "
				  "ret %u non_contiguous %u\n", ret, non_contiguous);
			*bad_wr = wr;
			ret = non_contiguous ? EINVAL : ret;
			break;
		}

		if (unlikely(addr % fl->fl_page_size)) {
			cstor_err(ucdev, CSTOR_NOLOG, "addr(%llu) is not aligned with "
				  "fl->fl_page_size : %u\n", addr, fl->fl_page_size);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		switch (fl->fl_page_size) {
		case 65536:
			addr |= CSTOR_FL_PAGE_SIZE_64K;
			break;
		}

		fl->queue[fl->pidx] = htobe64(addr);
		fl->sw_queue[fl->pidx].ctx = wr->ctx;
		fl->sw_queue[fl->pidx].addr = wr->sg_list->addr;
		t4_fl_produce(fl);
		fl->pend_cred++;
		if (unlikely((fl->pend_cred / 8) == max_pidx)) {
			t4_ring_fl_db(fl, ucdev->plat_dev);
			fl->pend_cred = 0;
		}
		num_wrs--;
		wr = wr->next;
	}

	t4_ring_fl_db(fl, ucdev->plat_dev);
	cstor_spin_unlock(&urxq->lock);

	return ret;
}

static void
cstor_cpl_iscsi_data(struct cstor_usock *ucsk, struct cpl_iscsi_data *cpl, void *fl_buf,
		     struct cstor_iscsi_wc *wc)
{
	struct t4_fl *fl = &ucsk->uqp->urxq->fl;

	if (unlikely(wc->qp)) {
		struct cstor_uqp *uqp = to_cstor_uqp(wc->qp);

		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "FATAL: TID mismatch! "
			  "unexpected CPL received for tid %u, CPL received is for tid %u\n",
			  uqp->ucsk->csk.tid, ucsk->csk.tid);
		abort();
	}

	wc->opcode = CSTOR_WC_OP_ISCSI_PDU;
	wc->data = fl_buf;
	wc->dctx = fl->sw_queue[fl->cidx].ctx;
	wc->dlen = be16toh(cpl->len);
	wc->qp = &ucsk->uqp->qp;
}

static inline void
cstor_process_ddpvld(struct cstor_iscsi_wc *wc, u32 ddpvld)
{
	wc->status |= (ddpvld & CSTOR_ISCSI_ERR_MASK);

	if ((ddpvld & (1U << CSTOR_ISCSI_DATA_DDP)) && (!wc->data))
		wc->data_ddp = 1;
}

static void
cstor_cpl_rx_iscsi_cmp(struct cstor_usock *ucsk, struct cpl_rx_iscsi_cmp *cpl,
		       struct cstor_iscsi_wc *wc)
{
	struct t4_fl *fl = &ucsk->uqp->urxq->fl;

	if (wc->qp) {
		struct cstor_uqp *uqp = to_cstor_uqp(wc->qp);

		if (unlikely(ucsk->csk.tid != uqp->ucsk->csk.tid)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "FATAL: TID mismatch! "
				  "unexpected CPL received for tid %u, "
				  "CPL received is for tid %u\n",
				  uqp->ucsk->csk.tid, ucsk->csk.tid);
			abort();
		}
	}

	wc->opcode = CSTOR_WC_OP_ISCSI_PDU;
	wc->hctx = fl->sw_queue[fl->cidx].ctx;
	wc->qp = &ucsk->uqp->qp;
	wc->hdr = cpl + 1;
	wc->hlen = be16toh(cpl->len);
	wc->seq = be32toh(cpl->seq);
	wc->ddgst = be32toh(cpl->ulp_crc);

	if (unlikely(ucsk->rcv_nxt != wc->seq)) {
		cstor_err(ucsk->ucdev, CSTOR_LOG, "tcp seq number mismatch, tid %u "
			  "expected %#x received %#x\n", ucsk->csk.tid, ucsk->rcv_nxt, wc->seq);
		wc->status = (1U << CSTOR_ISCSI_TCP_SEQ_MISMATCH_ERR);
	}

	ucsk->rcv_nxt += be16toh(cpl->pdu_len_ddp);
	ucsk->uqp->recv_bytes += be16toh(cpl->pdu_len_ddp);
	cstor_process_ddpvld(wc, be32toh(cpl->ddpvld));
}

static void cstor_cpl_rx_data(struct cstor_usock *ucsk, struct cstor_iscsi_wc *wc)
{
	if (unlikely(wc->qp)) {
		struct cstor_uqp *uqp = to_cstor_uqp(wc->qp);

		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "FATAL: TID mismatch! "
			  "unexpected CPL received for tid %u, CPL received is for tid %u\n",
			  uqp->ucsk->csk.tid, ucsk->csk.tid);
		abort();
	}

	wc->opcode = CSTOR_WC_OP_ISCSI_PDU;
	wc->status = (1U << CSTOR_ISCSI_RX_DATA_ERR);
	wc->qp = &ucsk->uqp->qp;

	*ucsk->uqp->wq.qp_errp = 1;
}

static int
cstor_process_iscsi_completion(struct cstor_usock *ucsk, struct cpl_rdma_cqe *cpl,
			       struct cstor_iscsi_wc *wc)
{
	void *ctx;
	struct t4_wq *wq = &ucsk->uqp->wq;
	u16 last_cidx = be32toh(cpl->msn) & 0xffff;

	assert(last_cidx < wq->sq.size);

	ctx = wq->sq.sw_sq[last_cidx].ctx;
	if (!ctx) {
		t4_sq_consume(wq, last_cidx);
		return EAGAIN;
	}

	if (wc->opcode) {
		if (unlikely(wc->opcode != CSTOR_WC_OP_ISCSI_PDU)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "Unexpected WC opcode(%#x)\n",
				  wc->opcode);
			abort();
		}
		ucsk->uqp->urxq->defer_wc = *wc;
		memset(wc, 0, sizeof(*wc));
	}

	if (G_CPL_RDMA_CQE_WR_TYPE(be32toh(cpl->qpid_to_wr_type)) == FW_RI_SEND)
		wc->opcode = CSTOR_WC_OP_SEND_CMPL;
	else
		wc->opcode = CSTOR_WC_OP_INVALIDATE_TAG_CMPL;

	wc->qp = &ucsk->uqp->qp;
	wc->free_ctx = ctx;
	t4_sq_consume(wq, last_cidx);
	return 0;
}

static int cstor_process_iscsi_flush_iqe(struct cstor_usock *ucsk, struct cstor_iscsi_wc *wc)
{
	if (wc->opcode) {
		if (unlikely(wc->opcode != CSTOR_WC_OP_ISCSI_PDU)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "Unexpected WC opcode(%#x)\n",
				  wc->opcode);
			abort();
		}
		ucsk->uqp->urxq->defer_wc = *wc;
		memset(wc, 0, sizeof(*wc));
	}

	wc->opcode = CSTOR_WC_OP_FLUSH;
	wc->qp = &ucsk->uqp->qp;
	return 0;
}

static int
cstor_cpl_rdma_cqe(struct cstor_usock *ucsk, struct cpl_rdma_cqe *cpl, struct cstor_iscsi_wc *wc)
{
	u16 wr_type = G_CPL_RDMA_CQE_WR_TYPE(be32toh(cpl->qpid_to_wr_type));

	switch (wr_type) {
	case FW_RI_SEND:
	case FW_RI_FAST_REGISTER:
	case FW_RI_LOCAL_INV:
		return cstor_process_iscsi_completion(ucsk, cpl, wc);
	case FW_RI_TERMINATE:
		return cstor_process_iscsi_flush_iqe(ucsk, wc);
	default:
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "error invalid wr_type %#x\n", wr_type);
		abort();
	}
}

static void
cstor_cpl_rdma_cqe_err(struct cstor_usock *ucsk, struct cpl_rdma_cqe_err *cpl,
		       struct cstor_iscsi_wc *wc)
{
	if (wc->opcode) {
		if (unlikely(wc->opcode != CSTOR_WC_OP_ISCSI_PDU)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "Unexpected WC opcode(%#x)\n",
				  wc->opcode);
			abort();
		}
		ucsk->uqp->urxq->defer_wc = *wc;
		memset(wc, 0, sizeof(*wc));
	}

	wc->opcode = CSTOR_WC_OP_SEND_ERR;
	wc->qp = &ucsk->uqp->qp;
	wc->status = G_CPL_RDMA_CQE_ERR_STATUS(be32toh(cpl->qpid_to_wr_type));

	*ucsk->uqp->wq.qp_errp = 1;
}

static int
cstor_iscsi_rx_handler(struct cstor_usock *ucsk, void *rsp, void *fl_buf, struct cstor_iscsi_wc *wc,
		       u8 *pending, u8 opcode)
{
	switch (opcode) {
	case CPL_ISCSI_DATA:
		cstor_cpl_iscsi_data(ucsk, rsp + 8, fl_buf, wc);
		*pending = 1;
		break;
	case CPL_RX_ISCSI_CMP:
		cstor_cpl_rx_iscsi_cmp(ucsk, fl_buf, wc);
		*pending = 0;
		break;
	case CPL_RX_DATA:
		cstor_cpl_rx_data(ucsk, wc);
		*pending = 0;
		break;
	case CPL_RDMA_CQE:
		if (cstor_cpl_rdma_cqe(ucsk, rsp, wc))
			return EAGAIN;
		*pending = 0;
		break;
	case CPL_RDMA_CQE_ERR:
		cstor_cpl_rdma_cqe_err(ucsk, rsp, wc);
		*pending = 0;
		break;
	}

	return 0;
}

static inline u32 cstor_get_tid(struct cstor_udevice *ucdev, void *rsp, void *fl_buf, u8 opcode)
{
	switch (opcode) {
	case CPL_RX_ISCSI_CMP:
	case CPL_RX_DATA:
		return GET_TID(((struct cpl_iscsi_data *)(fl_buf)));
	case CPL_ISCSI_DATA:
		return GET_TID(((struct cpl_iscsi_data *)(rsp + 8)));
	case CPL_RDMA_CQE:
	case CPL_RDMA_CQE_ERR:
		return G_CPL_RDMA_CQE_TID(be32toh(((struct cpl_rdma_cqe *)rsp)->tid_flitcnt));
	default:
		cstor_err(ucdev, CSTOR_NOLOG, "Unexpected CPL received, opcode(%#x)\n", opcode);
		abort();
	}
}

static int cstor_poll_rxq_one(struct cstor_urxq *urxq, struct cstor_iscsi_wc *wc, u8 *pending)
{
	struct cstor_usock *ucsk;
	struct cstor_udevice *ucdev = urxq->ucdev;
	struct t4_fl *fl = &urxq->fl;
	struct t4_iq *iq = &urxq->iq;
	struct rsp_ctrl *rc;
	struct t4_iqe *iqe;
	void *fl_buf = NULL;
	u32 length, tid;
	int ret;
	u8 opcode;

	ret = t4_next_iqe(iq, &iqe);
	if (ret)
		return ret;

	if (!(*pending))
		memset(wc, 0, sizeof(*wc));

	rc = (void *)iqe + (iq->iqe_len - sizeof(*rc));
	if (G_RSPD_TYPE(rc->u.type_gen) == X_RSPD_TYPE_FLBUF) {
		length = be32toh(rc->pldbuflen_qid);
		if (length & F_RSPD_NEWBUF) {
			if (likely(fl->offset > 0)) {
				wc->free_ctx = fl->sw_queue[fl->cidx].ctx;
				t4_fl_consume(fl);
				fl->offset = 0;
			}
			length = G_RSPD_LEN(length);
		}
		fl_buf = (void *)fl->sw_queue[fl->cidx].addr + fl->offset;
		fl->offset += align(length, fl->fl_align);
	}

	opcode = *((u8 *)iqe);
	tid = cstor_get_tid(ucdev, iqe, fl_buf, opcode);

	ucsk = get_sock(ucdev, tid);
	if (unlikely(!ucsk)) {
		cstor_err(ucdev, CSTOR_NOLOG, "ucsk is NULL for tid %u, opcode(%#x)\n",
			  tid, opcode);
		abort();
	}

	cstor_spin_lock(&ucsk->uqp->lock);
	ret = cstor_iscsi_rx_handler(ucsk, iqe, fl_buf, wc, pending, opcode);
	cstor_spin_unlock(&ucsk->uqp->lock);

	t4_iq_consume(iq, ucdev->plat_dev);

	return ret;
}

int cstor_poll_rxq(struct cstor_rxq *rxq, struct cstor_iscsi_wc *wc)
{
	struct cstor_urxq *urxq = to_cstor_urxq(rxq);
	struct cstor_iscsi_wc *dwc = &urxq->defer_wc;
	int ret;
	u8 pending = 0;

	if (unlikely(!urxq->mapped)) {
		cstor_err(urxq->ucdev, CSTOR_NOLOG, "rxqid %u is not mapped\n", rxq->rxqid);
		return EINVAL;
	}

	cstor_spin_lock(&urxq->lock);
	if (dwc->opcode) {
		if (unlikely(dwc->opcode != CSTOR_WC_OP_ISCSI_PDU)) {
			cstor_err(urxq->ucdev, CSTOR_NOLOG, "Unexpected WC opcode(%#x)\n",
				  dwc->opcode);
			abort();
		}
		*wc = *dwc;
		memset(dwc, 0, sizeof(*dwc));
		pending = 1;
	}

	do {
		ret = cstor_poll_rxq_one(urxq, wc, &pending);
	} while (pending || ret == EAGAIN);
	cstor_spin_unlock(&urxq->lock);

	return ret;
}
