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
#include "cstor_ioctl.h"

static const char *cstor_send_status_str[CSTOR_SEND_ERR_MAX] = {
	[CSTOR_SEND_SUCCESS]				= "send success",
	[CSTOR_SEND_ERR_INVALID_STAG]			= "invalid STAG err",
	[CSTOR_SEND_ERR_PDID_MISMATCH]			= "PDID mismatch err",
	[CSTOR_SEND_ERR_QPID_MISMATCH]			= "QPID mismatch err",
	[CSTOR_SEND_ERR_ACCESS]				= "access err",
	[CSTOR_SEND_ERR_WRAP]				= "wrap error",
	[CSTOR_SEND_ERR_BOUND]				= "bound err",
	[CSTOR_SEND_ERR_INVALIDATE_SHARED_MR]		= "invalidate shared MR err",
	[CSTOR_SEND_ERR_INVALIDATE_MR_WITH_MW_BOUND]	= "invalidate MR with MW bound err",
	[CSTOR_SEND_ERR_ECC]				= "ECC err",
	[CSTOR_SEND_ERR_ECC_PSTAG]			= "ECC PSTAG err",
	[CSTOR_SEND_ERR_PBL_ADDR_BOUND]			= "PBL addr bound err",
	[CSTOR_SEND_ERR_CRC]				= "CRC err",
	[CSTOR_SEND_ERR_MARKER]				= "marker err",
	[CSTOR_SEND_ERR_PDU_LEN_ERR]			= "invalid PDU len err",
	[CSTOR_SEND_ERR_OUT_OF_RQE]			= "out of RQE err",
	[CSTOR_SEND_ERR_DDP_VERSION]			= "invalid DDP version err",
	[CSTOR_SEND_ERR_RDMA_VERSION]			= "invalid RDMA version err",
	[CSTOR_SEND_ERR_RDMA_OPCODE]			= "invalid RDMA opcode err",
	[CSTOR_SEND_ERR_DDP_QUEUE_NUM]			= "invalid DDP queue num err",
	[CSTOR_SEND_ERR_MSN]				= "MSN err",
	[CSTOR_SEND_ERR_TBIT]				= "tag bit err",
	[CSTOR_SEND_ERR_MO]				= "MO nonzero err",
	[CSTOR_SEND_ERR_RQE_ADDR_BOUND]			= "RQE addr bound err",
	[CSTOR_SEND_ERR_INTERNAL]			= "internal err",
};

const char *cstor_get_send_status_str(u8 status)
{
	if (unlikely(status >= CSTOR_SEND_ERR_MAX)) {
		errno = EINVAL;
		return NULL;
	}

	return cstor_send_status_str[status];
}

static const char *cstor_nvme_tcp_status[CSTOR_NVME_TCP_MAX_STATUS] = {
	[CSTOR_NVME_TCP_SUCCESS] = "success",
	[CSTOR_NVME_TCP_HDGST_ERR] = "header digest err",
	[CSTOR_NVME_TCP_DDGST_ERR] = "data digest err",
	[CSTOR_NVME_TCP_DIR_ERR] = "direction mismatch err",
	[CSTOR_NVME_TCP_DGST_FLAG_ERR] = "digest flag err",
	[CSTOR_NVME_TCP_C2H_SUCCESS_BIT_ERR] = "c2h success bit error",
	[CSTOR_NVME_TCP_CMD_DATA_LEN_ERR] = "cmd data len err",

	[CSTOR_NVME_TCP_RQT_LIMIT_ERR] = "rqt limit err",
	[CSTOR_NVME_TCP_RQT_WRAP_ERR] = "rqt wrap err",
	[CSTOR_NVME_TCP_RQT_SIZE_ERR] = "rqt size err",

	[CSTOR_NVME_TCP_TPT_LIMIT_ERR] = "tpt limit err",
	[CSTOR_NVME_TCP_TPT_INVALID_ERR] = "tpt invalid err",
	[CSTOR_NVME_TCP_TPT_COLOR_ERR] = "tpt color err",
	[CSTOR_NVME_TCP_TPT_PROT_ERR] = "tpt protection err",
	[CSTOR_NVME_TCP_TPT_WRAP_ERR] = "tpt wrap err",
	[CSTOR_NVME_TCP_TPT_BOUND_ERR] = "tpt bound err",
	[CSTOR_NVME_TCP_TPT_LPDU_UNALIGNED_ERR] = "tpt last pdu unaligned err",
};

const char *cstor_get_nvme_tcp_status_str(u8 status)
{
	if (status >= CSTOR_NVME_TCP_MAX_STATUS) {
		errno = EINVAL;
		return NULL;
	}

	return cstor_nvme_tcp_status[status];
}

int __cstor_destroy_cq(struct cstor_udevice *ucdev, u32 cqid)
{
	struct cstor_destroy_cq_cmd cmd = {};
	int ret;

	cmd.cqid = cqid;
	cstor_debug(ucdev, CSTOR_NOLOG, "cqid %u\n", cqid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DESTROY_CQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DESTROY_CQ cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

struct cstor_cq *cstor_create_cq(struct cstor_device *cdev, struct cstor_cq_attr *attr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_ucq *ucq;
	struct cstor_create_cq_cmd cmd = {};
	int ret;

	ucq = calloc(1, sizeof(*ucq));
	if (!ucq) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucq allocation\n");
		errno = ENOMEM;
		return NULL;
	}

	cmd.cqe_size = sizeof(*ucq->q.queue);
	cmd.num_cqe = attr->num_cqe;
	cmd.event_fd = (attr->event_fd != -1) ? attr->event_fd : INVALID_EVENT_FD;

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_CREATE_CQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_CQ cmd, "
			  "ret %d\n", ret);
		goto err1;
	}

	ucq->cq.cdev = cdev;
	ucq->cq.cqid = cmd.resp.cqid;
	ucq->cq.num_cqe = attr->num_cqe;
	ucq->cq.event_fd = attr->event_fd;

	cstor_spin_init(&ucq->lock, attr->no_lock);
	ucq->ucdev = ucdev;
	ucq->q.qid_mask = cmd.resp.qid_mask;
	ucq->q.cqid = cmd.resp.cqid;
	ucq->q.size = cmd.resp.size;
	ucq->q.memsize = cmd.resp.memsize;
	ucq->q.gen = 1;
	ucq->q.max_cidx_inc = min((ucq->q.size >> 4), ucdev->plat_dev ? M_ARM_CIDXINC : M_CIDXINC);
	ucq->q.queue = mmap(NULL, ucq->q.memsize, PROT_READ | PROT_WRITE, MAP_SHARED,
			    cdev->dev_fd, cmd.resp.key);
	if (ucq->q.queue == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, ucq->q.memsize %lu "
			  "cmd.resp.key %llu\n", ucq->q.memsize, cmd.resp.key);
		goto err2;
	}

	ucq->q.ugts = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
			   cdev->dev_fd, cmd.resp.gts_key);
	if (ucq->q.ugts == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
			  "cmd.resp.gts_key %llu\n", cstor_page_size, cmd.resp.gts_key);
		goto err3;
	}

	if (!ucdev->plat_dev)
		ucq->q.ugts += 5;

	ucq->mapped = true;

	cstor_debug(ucdev, CSTOR_LOG, "cqid %#x key %llu va %p memsize %lu gts_key %llu "
		    "va %p qid_mask %#x\n", ucq->q.cqid, cmd.resp.key, ucq->q.queue,
		    ucq->q.memsize, cmd.resp.gts_key, ucq->q.ugts, ucq->q.qid_mask);

	return &ucq->cq;

err3:
	munmap(ucq->q.queue, ucq->q.memsize);
err2:
	__cstor_destroy_cq(ucdev, ucq->q.cqid);
err1:
	free(ucq);
	errno = ret;
	return NULL;
}

int cstor_destroy_cq(struct cstor_cq *cq)
{
	struct cstor_ucq *ucq = to_cstor_ucq(cq);
	int ret;

	if (ucq->mapped) {
		munmap(MASKED(ucq->q.ugts), cstor_page_size);
		munmap(ucq->q.queue, ucq->q.memsize);
		ucq->mapped = false;
	}

	ret = __cstor_destroy_cq(ucq->ucdev, ucq->q.cqid);
	if (ret) {
		cstor_err(ucq->ucdev, CSTOR_NOLOG, "__cstor_destroy_cq() failed, cqid %u\n",
			  ucq->q.cqid);
		return ret;
	}

	free(ucq);
	return 0;
}

static void
cstor_cpl_nvmt_cmp_imm(struct cstor_usock *ucsk, struct cpl_nvmt_cmp_imm *cpl,
		       struct cstor_nvme_tcp_wc *wc)
{
	struct cstor_uqp *uqp = ucsk->uqp;
	struct t4_wq *wq = &uqp->wq;
	u16 len = be16toh(cpl->length);
	u8 data_in_rq = G_CPL_NVMT_CMP_IMM_OPRQINC(be32toh(cpl->generation_bit_to_oprqinc)) & 1;

	wc->opcode = CSTOR_WC_OP_NVME_TCP_PDU;
	wc->status = cpl->status;
	wc->data_ddp = cpl->status >> 7;
	wc->seq = be32toh(cpl->seq);
	wc->qp = &uqp->qp;

	if (unlikely(!len && !data_in_rq)) {
		wc->status = CSTOR_NVME_TCP_LEN_ERR;
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "error !len !data_in_rq, cpl->status %u, "
			  "tid %u\n", cpl->status, ucsk->csk.tid);
		abort();
	}

	if (len) {
		memcpy(wc->hdr, (u8 *)cpl + 24, len);
		wc->hlen = len;
	}

	if (data_in_rq) {
		assert(wq->rq.wr_cidx < wq->rq.max_wr);
		wc->ctx = wq->rq.sw_rq[wq->rq.wr_cidx].ctx;
		assert(!t4_rq_empty(wq));
		t4_rq_consume(wq);
	}
}

static void
cstor_cpl_nvmt_cmp_srq(struct cstor_usock *ucsk, struct cpl_nvmt_cmp_srq *cpl,
		       struct cstor_nvme_tcp_wc *wc)
{
	struct cstor_uqp *uqp = ucsk->uqp;
	u16 len = be16toh(cpl->length);
	u8 data_in_rq = G_CPL_NVMT_CMP_IMM_OPRQINC(be32toh(cpl->generation_bit_to_oprqinc)) & 1;

	wc->opcode = CSTOR_WC_OP_NVME_TCP_PDU;
	wc->status = cpl->status;
	wc->data_ddp = cpl->status >> 7;
	wc->seq = be32toh(cpl->seq);
	wc->qp = &uqp->qp;

	if (unlikely(!len && !data_in_rq)) {
		wc->status = CSTOR_NVME_TCP_LEN_ERR;
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "error !len !data_in_rq, cpl->status %u, "
			  "tid %u\n", cpl->status, ucsk->csk.tid);
		abort();
	}

	if (len) {
		memcpy(wc->hdr, cpl + 1, len);
		wc->hlen = len;
	}

	if (data_in_rq) {
		struct cstor_usrq *usrq = uqp->usrq;
		struct t4_srq *wq = &usrq->wq;
		u32 rqe = be32toh(cpl->rqe);
		int rel_idx = rqe - wq->rqt_abs_idx;

		assert(rel_idx < wq->max_wr);
		assert(wq->sw_rq[rel_idx].valid);
		wq->sw_rq[rel_idx].valid = false;

		wc->ctx = wq->sw_rq[rel_idx].ctx;
		t4_srq_consume(wq);
	}
}

static int
cstor_process_send_cmp(struct cstor_usock *ucsk, struct cpl_rdma_cqe *cpl,
		       struct cstor_nvme_tcp_wc *wc)
{
	struct cstor_uqp *uqp = ucsk->uqp;
	struct t4_wq *wq = &uqp->wq;
	u16 last_cidx = be32toh(cpl->msn) & 0xffff;

	assert(last_cidx < wq->sq.size);

	wc->ctx = wq->sq.sw_sq[last_cidx].ctx;
	if (!wc->ctx) {
		t4_sq_consume(wq, last_cidx);
		return EAGAIN;
	}

	if (G_CPL_RDMA_CQE_WR_TYPE(be32toh(cpl->qpid_to_wr_type)) == FW_RI_SEND)
		wc->opcode = CSTOR_WC_OP_SEND_CMPL;
	else
		wc->opcode = CSTOR_WC_OP_INVALIDATE_TAG_CMPL;

	wc->qp = &uqp->qp;

	t4_sq_consume(wq, last_cidx);
	return 0;
}

static void
cstor_process_flush_cqe(struct cstor_usock *ucsk, struct cstor_nvme_tcp_wc *wc)
{
	wc->opcode = CSTOR_WC_OP_FLUSH;
	wc->qp = &ucsk->uqp->qp;
}

static int
cstor_cpl_rdma_cqe(struct cstor_usock *ucsk, struct cpl_rdma_cqe *cpl,
		   struct cstor_nvme_tcp_wc *wc)
{
	u16 wr_type = G_CPL_RDMA_CQE_WR_TYPE(be32toh(cpl->qpid_to_wr_type));

	switch (wr_type) {
	case FW_RI_SEND:
	case FW_RI_FAST_REGISTER:
	case FW_RI_LOCAL_INV:
		return cstor_process_send_cmp(ucsk, cpl, wc);
	case FW_RI_TERMINATE:
		cstor_process_flush_cqe(ucsk, wc);
		return 0;
	default:
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "invalid wr_type %#x, tid %u\n",
			  wr_type, ucsk->csk.tid);
		abort();
	}
}

static void
cstor_cpl_rdma_cqe_err(struct cstor_usock *ucsk, struct cpl_rdma_cqe_err *cpl,
		       struct cstor_nvme_tcp_wc *wc)
{
	wc->opcode = CSTOR_WC_OP_SEND_ERR;
	wc->qp = &ucsk->uqp->qp;
	wc->status = G_CPL_RDMA_CQE_ERR_STATUS(be32toh(cpl->qpid_to_wr_type));

	*ucsk->uqp->wq.qp_errp = 1;
}

/*
 * Get one cq entry from cstor and map it to openib.
 *
 * Returns:
 *	0			cqe returned
 *	-ENODATA		EMPTY;
 *	-EAGAIN			caller must try again
 */
static int cstor_poll_cq_one(struct cstor_ucq *ucq, struct cstor_nvme_tcp_wc *wc)
{
	struct cstor_usock *ucsk;
	struct cstor_uqp *uqp;
	struct cstor_usrq *usrq;
	struct t4_cqe *cqe;
	struct cpl_nvmt_cmp_imm *cpl;
	u32 tid, opcode;
	int ret;

	ret = t4_next_cqe(&ucq->q, &cqe);
	if (ret)
		return ret;

	cpl = (void *)cqe;
	opcode = G_CPL_NVMT_CMP_IMM_OPCODE(be32toh(cpl->op_to_cqid));
	tid = G_CPL_NVMT_CMP_IMM_TID(be32toh(cpl->generation_bit_to_oprqinc));

	ucsk = get_sock(ucq->ucdev, tid);
	if (unlikely(!ucsk)) {
		cstor_err(ucq->ucdev, CSTOR_NOLOG, "ucsk is NULL for tid %u, opcode %#x\n",
			  tid, opcode);
		abort();
	}
	uqp = ucsk->uqp;

	cstor_spin_lock(&uqp->lock);
	usrq = uqp->usrq;
	if (usrq)
		cstor_spin_lock(&usrq->lock);

	memset(wc, 0, sizeof(*wc));

	switch (opcode) {
	case CPL_NVMT_CMP_IMM:
		cstor_cpl_nvmt_cmp_imm(ucsk, cpl, wc);
		break;
	case CPL_NVMT_CMP_SRQ:
		cstor_cpl_nvmt_cmp_srq(ucsk, (void *)cpl, wc);
		break;
	case CPL_RDMA_CQE:
		if (cstor_cpl_rdma_cqe(ucsk, (void *)cpl, wc))
			ret = EAGAIN;
		break;
	case CPL_RDMA_CQE_ERR:
		cstor_cpl_rdma_cqe_err(ucsk, (void *)cpl, wc);
		break;
	default:
		cstor_err(usrq->ucdev, CSTOR_NOLOG, "error invalid opcode(%#x) received, tid %u\n",
			  opcode, tid);
		abort();
	}

	t4_cq_consume(&ucq->q, ucq->ucdev->plat_dev);
	if (usrq)
		cstor_spin_unlock(&usrq->lock);

	cstor_spin_unlock(&uqp->lock);
	return ret;
}

int cstor_poll_cq(struct cstor_cq *cq, struct cstor_nvme_tcp_wc *wc)
{
	struct cstor_ucq *ucq = to_cstor_ucq(cq);
	int ret;

	if (unlikely(!ucq->mapped)) {
		cstor_err(ucq->ucdev, CSTOR_NOLOG, "cqid %u is not mapped\n", cq->cqid);
		return EINVAL;
	}

	cstor_spin_lock(&ucq->lock);
	do {
		ret = cstor_poll_cq_one(ucq, wc);
	} while (ret == EAGAIN);
	cstor_spin_unlock(&ucq->lock);

	return ret;
}

int cstor_arm_cq(struct cstor_cq *cq)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cq->cdev);
	struct cstor_ucq *ucq;
	int ret;

	if (unlikely(cq->event_fd == -1)) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid cq->event_fd is %d\n", cq->event_fd);
		return EINVAL;
	}

	ucq = to_cstor_ucq(cq);
	if (unlikely(!ucq->mapped)) {
		cstor_err(ucq->ucdev, CSTOR_NOLOG, "cqid %u is not mapped\n", cq->cqid);
		return EINVAL;
	}
	cstor_spin_lock(&ucq->lock);
	ret = t4_arm_cq(&ucq->q, 0, ucdev->plat_dev);
	cstor_spin_unlock(&ucq->lock);
	return ret;
}
