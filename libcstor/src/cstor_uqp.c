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

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "cstor_umain.h"
#include "cstor_uiscsi_ddp.h"

static int __cstor_destroy_qp(struct cstor_udevice *ucdev, u32 qid)
{
	struct cstor_destroy_qp_cmd cmd = {};
	int ret;

	cmd.qid = qid;
	cstor_debug(ucdev, CSTOR_NOLOG, "cmd.qid %u\n", qid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DESTROY_QP, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DESTROY_QP cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int cstor_setup_nvme_tcp_ddp_tags(struct cstor_ddp_tag *ddp)
{
	u32 bits_in_long = sizeof(unsigned long) * 8;
	u32 num_long = DIV_ROUND_UP(ddp->num_tags, bits_in_long);
	u32 i;

	ddp->tag_bm = calloc(num_long, sizeof(unsigned long));
	if (!ddp->tag_bm) {
		cstor_printf(stderr, CSTOR_NOLOG, "failed ddp->tag_bm allocation, num_long %u\n",
			     num_long);
		return ENOMEM;
	}

	memset(ddp->tag_bm, 0xff, num_long * sizeof(unsigned long));

	ddp->color = calloc(ddp->num_tags, sizeof(u8));
	if (!ddp->color) {
		cstor_printf(stderr, CSTOR_NOLOG, "failed ddp->color allocation, "
			     "ddp->num_tags %u\n", ddp->num_tags);
		free(ddp->tag_bm);
		return ENOMEM;
	}

	for (i = 0; i < ddp->num_tags; i++)
		ddp->color[i] = 1;

	ddp->num_long = num_long;
	return 0;
}

struct cstor_qp *cstor_create_qp(struct cstor_pd *pd, struct cstor_qp_attr *attr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(pd->cdev);
	struct cstor_uqp *uqp;
	struct cstor_ddp_tag *ddp;
	void *dbva;
	struct cstor_create_qp_cmd cmd = {};
	unsigned long segment_offset;
	bool need_rq;
	int ret;

	cstor_debug(ucdev, CSTOR_LOG, "\n");
	uqp = calloc(1, sizeof(*uqp));
	if (!uqp) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed uqp allocation\n");
		ret = ENOMEM;
		goto err1;
	}

	cmd.pdid = pd->pdid;

	switch (attr->protocol) {
	case CSTOR_NVME_TCP_PROTOCOL:
		cmd.protocol = _CSTOR_NVME_TCP_PROTOCOL;
		if (!attr->send_cq || !attr->recv_cq) {
			cstor_err(ucdev, CSTOR_NOLOG, "attr->send_cq %p or "
				  "attr->recv_cq %p is NULL\n", attr->send_cq, attr->recv_cq);
			ret = EINVAL;
			goto err2;
		}

		cmd.scqid = attr->send_cq->cqid;
		cmd.rcqid = attr->recv_cq->cqid;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u scqid %u rcqid %u\n",
			    attr->protocol, cmd.scqid, cmd.rcqid);
		break;
	case CSTOR_ISCSI_PROTOCOL:
		cmd.protocol = _CSTOR_ISCSI_PROTOCOL;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u\n", attr->protocol);
		break;
	default:
		cstor_err(ucdev, CSTOR_NOLOG, "invalid protocol %u\n", attr->protocol);
		ret = EINVAL;
		goto err2;
	}

	cmd.srqid = attr->srq ? attr->srq->srqid : CSTOR_INVALID_SRQID;
	cmd.rxqid = attr->rxq ? attr->rxq->rxqid : CSTOR_INVALID_RXQID;
	need_rq = !attr->srq && !attr->rxq;

	cmd.max_send_wr = attr->max_send_wr;
	cmd.max_recv_wr = attr->max_recv_wr;
	cmd.max_ddp_sge = attr->max_ddp_sge;
	cmd.max_ddp_tag = attr->max_ddp_tag;

	cstor_debug(ucdev, CSTOR_NOLOG, "pdid %u srqid %u rxqid %u max_send_wr %u "
		    "max_recv_wr %u max_ddp_sge %u max_ddp_tag %u\n",
		    cmd.pdid, cmd.srqid, cmd.rxqid, cmd.max_send_wr, cmd.max_recv_wr,
		    cmd.max_ddp_sge, cmd.max_ddp_tag);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_CREATE_QP, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_QP cmd, "
			  "ret %d\n", ret);
		goto err2;
	}

	cstor_debug(ucdev, CSTOR_LOG, "sqid 0x%x sq key %llu sq db/gts key %llu "
		    "rqid 0x%x rq key %llu rq db/gts key %llu qid_mask 0x%x\n",
		    cmd.resp.sqid, cmd.resp.sq_key, cmd.resp.sq_db_key, cmd.resp.rqid,
		    cmd.resp.rq_key, cmd.resp.rq_db_key, cmd.resp.qid_mask);

	uqp->ucdev = ucdev;
	uqp->usrq = attr->srq ? to_cstor_usrq(attr->srq) : NULL;
	uqp->urxq = attr->rxq ? to_cstor_urxq(attr->rxq) : NULL;
	uqp->qp.ctx = attr->ctx;
	uqp->qp.pd = pd;
	uqp->qp.send_cq = attr->send_cq;
	uqp->qp.recv_cq = attr->recv_cq;
	uqp->qp.srq = attr->srq;
	uqp->qp.qpid = cmd.resp.sqid;

	uqp->wq.qid_mask = cmd.resp.qid_mask;
	uqp->wq.sq.qid = cmd.resp.sqid;
	uqp->wq.sq.size = cmd.resp.sq_size;
	uqp->wq.sq.memsize = cmd.resp.sq_memsize;
	if (need_rq) {
		uqp->wq.rq.qid = cmd.resp.rqid;
		uqp->wq.rq.size = cmd.resp.rq_size;
		uqp->wq.rq.max_wr = cmd.resp.rq_max_wr;
		uqp->wq.rq.memsize = cmd.resp.rq_memsize;
	}

	uqp->auto_cmpl = attr->auto_cmpl;
	uqp->pend_cmpl_ndesc = 0;
	cstor_spin_init(&uqp->lock, attr->no_lock);

	dbva = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
		    ucdev->cdev.dev_fd, cmd.resp.sq_db_key);
	if (dbva == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
			  "cmd.resp.sq_db_key %llu\n", cstor_page_size, cmd.resp.sq_db_key);
		goto err3;
	}

	uqp->wq.sq.udb = dbva;

	if (!ucdev->plat_dev) {
		segment_offset = 128 * (uqp->wq.sq.qid & uqp->wq.qid_mask);
		if (segment_offset < cstor_page_size) {
			uqp->wq.sq.udb += segment_offset / 4;
			uqp->wq.sq.wc_reg_available = true;
		} else {
			uqp->wq.sq.bar2_qid = uqp->wq.sq.qid & uqp->wq.qid_mask;
		}

		uqp->wq.sq.udb += 2;
	}

	uqp->wq.sq.queue = mmap(NULL, uqp->wq.sq.memsize, PROT_WRITE, MAP_SHARED,
				ucdev->cdev.dev_fd, cmd.resp.sq_key);
	if (uqp->wq.sq.queue == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, uqp->wq.sq.memsize %lu "
			   "cmd.resp.sq_key %llu\n", uqp->wq.sq.memsize,
			   cmd.resp.sq_key);
		goto err4;
	}

	if (need_rq) {
		dbva = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
			    ucdev->cdev.dev_fd, cmd.resp.rq_db_key);
		if (dbva == MAP_FAILED) {
			ret = errno;
			cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
				  "cmd.resp.rq_db_key %llu\n", cstor_page_size, cmd.resp.rq_db_key);
			goto err5;
		}

		uqp->wq.rq.udb = dbva;

		if (!ucdev->plat_dev) {
			segment_offset = 128 * (uqp->wq.rq.qid & uqp->wq.qid_mask);

			if (segment_offset < cstor_page_size) {
				uqp->wq.rq.udb += segment_offset / 4;
				uqp->wq.rq.wc_reg_available = true;
			} else {
				uqp->wq.rq.bar2_qid = uqp->wq.rq.qid & uqp->wq.qid_mask;
			}

			uqp->wq.rq.udb += 2;
		}

		uqp->wq.rq.queue = mmap(NULL, uqp->wq.rq.memsize, PROT_WRITE, MAP_SHARED,
					ucdev->cdev.dev_fd, cmd.resp.rq_key);
		if (uqp->wq.rq.queue == MAP_FAILED) {
			ret = errno;
			cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, uqp->wq.rq.memsize %lu "
				  "cmd.resp.rq_key %llu\n", uqp->wq.rq.memsize, cmd.resp.rq_key);
			goto err6;
		}
	}

	uqp->mapped = true;

	uqp->wq.sq.sw_sq = calloc(uqp->wq.sq.size, sizeof(struct t4_swsqe));
	if (!uqp->wq.sq.sw_sq) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed uqp->wq.sq.sw_sq allocation, "
			  "uqp->wq.sq.size %u\n", uqp->wq.sq.size);
		ret = ENOMEM;
		goto err7;
	}

	if (need_rq) {
		uqp->wq.rq.sw_rq = calloc(uqp->wq.rq.max_wr, sizeof(struct t4_swrqe));
		if (!uqp->wq.rq.sw_rq) {
			cstor_err(ucdev, CSTOR_NOLOG, "failed uqp->wq.rq.sw_rq allocation, "
				  "uqp->wq.rq.max_wr %u\n", uqp->wq.rq.max_wr);
			ret = ENOMEM;
			goto err8;
		}
		uqp->wq.qp_errp = &uqp->wq.rq.queue[uqp->wq.rq.max_wr].status.qp_err;
	} else {
		uqp->wq.qp_errp = &((union t4_wr *)((void *)uqp->wq.sq.queue +
				  (uqp->wq.sq.size * T4_EQ_ENTRY_SIZE)))->status.qp_err;
	}

	if (attr->protocol == CSTOR_NVME_TCP_PROTOCOL) {
		ddp = &uqp->ddp;
		ddp->num_tags = cmd.max_ddp_tag;
		ddp->stag_idx = cmd.resp.stag_idx;
		cstor_spin_init(&ddp->lock, attr->no_lock);

		ret = cstor_setup_nvme_tcp_ddp_tags(ddp);
		if (ret) {
			cstor_err(ucdev, CSTOR_NOLOG, "cstor_setup_nvme_tcp_ddp_tags() failed, "
				  "ret %d\n", ret);
			ret = ENOMEM;
			goto err9;
		}

		uqp->pbl_offset = cmd.resp.pbl_offset;
		uqp->max_ddp_sge = cmd.max_ddp_sge;
	}

	cstor_debug(ucdev, CSTOR_LOG, "sq dbva %p sq qva %p sq depth %u sq memsize %lu "
		    "rq dbva %p rq qva %p rq depth %u rq memsize %lu\n", uqp->wq.sq.udb,
		    uqp->wq.sq.queue, uqp->wq.sq.size, uqp->wq.sq.memsize, uqp->wq.rq.udb,
		    uqp->wq.rq.queue, uqp->wq.rq.max_wr, uqp->wq.rq.memsize);

	return &uqp->qp;
err9:
	free(uqp->wq.rq.sw_rq);
err8:
	free(uqp->wq.sq.sw_sq);
err7:
	if (need_rq)
		munmap(uqp->wq.rq.queue, uqp->wq.rq.memsize);
err6:
	if (need_rq)
		munmap(MASKED(uqp->wq.rq.udb), cstor_page_size);
err5:
	munmap(uqp->wq.sq.queue, uqp->wq.sq.memsize);
err4:
	munmap(MASKED(uqp->wq.sq.udb), cstor_page_size);
err3:
	__cstor_destroy_qp(ucdev, uqp->wq.sq.qid);
err2:
	free(uqp);
err1:
	errno = ret;
	return NULL;
}

int cstor_destroy_qp(struct cstor_qp *qp)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(qp->pd->cdev);
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	int ret;

	cstor_debug(ucdev, CSTOR_LOG, "qp with qid %u\n", uqp->wq.sq.qid);

	if (uqp->mapped) {
		munmap(MASKED(uqp->wq.sq.udb), cstor_page_size);
		munmap(uqp->wq.sq.queue, uqp->wq.sq.memsize);
		if (!uqp->usrq) {
			munmap(MASKED(uqp->wq.rq.udb), cstor_page_size);
			munmap(uqp->wq.rq.queue, uqp->wq.rq.memsize);
			free(uqp->wq.rq.sw_rq);
		}
		uqp->mapped = false;
	}

	ret = __cstor_destroy_qp(ucdev, uqp->wq.sq.qid);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "__cstor_destroy_qp() failed, "
			  "qid %u\n", uqp->wq.sq.qid);
		return ret;
	}

	free(uqp->ddp.color);
	free(uqp->ddp.tag_bm);

	free(uqp->wq.sq.sw_sq);
	free(uqp);
	return 0;
}

void copy_wr_to_queue(void *dst, void *src, void *queue_start, void *queue_end, u8 len16)
{
	u32 len = queue_end - dst;

	memcpy(dst, src, len);
	memcpy(queue_start, src + len, (len16 << 4) - len);
}

static int build_immd(void *immdp, struct cstor_send_wr *wr, u32 *plenp, u32 *num_imm_sge)
{
	u8 *srcp;
	u8 *dstp = (u8 *)immdp;
	u32 plen = 0;
	u32 len, i;

	*num_imm_sge = 0;
	for (i = 0; i < wr->num_sge; i++) {
		if (wr->sg_list[i].lkey)
			break;

		if (unlikely((plen + wr->sg_list[i].length) > T4_MAX_SEND_IMM_DATA)) {
			cstor_printf(stderr, CSTOR_NOLOG, "invalid, is less than "
				     "sum of plen %u wr->sg_list[%d].length %u\n",
				     plen, i, wr->sg_list[i].length);
			return EMSGSIZE;
		}

		srcp = (u8 *)wr->sg_list[i].addr;
		plen += wr->sg_list[i].length;
		len = wr->sg_list[i].length;

		memcpy(dstp, srcp, len);
		dstp += len;
		(*num_imm_sge)++;
	}

	*plenp = plen;
	return 0;
}

static int
build_ulptx_sgl(struct cstor_udevice *ucdev, struct ulptx_sgl *sgl,
		struct cstor_sge *sg_list, u32 *num_dsgl_sge)
{
	struct cstor_sge sge;
	u64 addr;
	u32 length;
	u32 len = 0;
	int ret = 0;
	u8 idx = 0;
	bool non_contiguous;

	*num_dsgl_sge = 0;
	sge = *sg_list;
	while (sge.length) {
		if (unlikely(*num_dsgl_sge >= 3)) {
			cstor_err(ucdev, CSTOR_NOLOG, "invalid num_dsgl_sge %u >= 3\n",
				  *num_dsgl_sge);
			return EINVAL;
		}

		ret = cstor_virt_to_dma_addr(ucdev, &sge, &addr, &length, &non_contiguous);
		if (unlikely(ret)) {
			cstor_err(ucdev, CSTOR_NOLOG, "cstor_virt_to_dma_addr() failed, "
				  "ret %d\n", ret);
			return ret;
		}

		if (!len) {
			sgl->len0 = htobe32(length);
			sgl->addr0 = htobe64(addr);
		} else {
			sgl->sge->len[idx] = htobe32(length);
			sgl->sge->addr[idx++] = htobe64(addr);
		}

		(*num_dsgl_sge)++;
		sge.addr += length;
		sge.length -= length;
		len += length;
	}

	sgl->cmd_nsge = htobe32(V_ULPTX_CMD(ULP_TX_SC_DSGL) | V_ULPTX_NSGE(*num_dsgl_sge));

	return 0;
}

static int build_isgl(struct fw_ri_isgl *isglp, struct cstor_sge *sg_list, u32 num_sge, u32 *plenp)
{
	__be64 *flitp = (__be64 *)isglp->sge;
	u32 plen = 0;
	u32 i;

	for (i = 0; i < num_sge; i++) {
		if (unlikely(!sg_list[i].lkey)) {
			cstor_printf(stderr, CSTOR_NOLOG, "invalid sg_list[%u].lkey %u\n",
				     i, sg_list[i].lkey);
			return EINVAL;
		}

		if (unlikely((plen + sg_list[i].length) < plen)) {
			cstor_printf(stderr, CSTOR_NOLOG, "invalid plen %u "
				     "sg_list[%d].length %u\n", plen, i, sg_list[i].length);
			return EMSGSIZE;
		}

		plen += sg_list[i].length;
		*flitp++ = htobe64(((u64)sg_list[i].lkey << 32) | sg_list[i].length);
		*flitp++ = htobe64(sg_list[i].addr);
	}

	*flitp = 0;
	isglp->op = FW_RI_DATA_ISGL;
	isglp->r1 = 0;
	isglp->nsge = htobe16(num_sge);
	isglp->r2 = 0;
	if (plenp)
		*plenp = plen;

	return 0;
}

static int
build_tx_data_wr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
		 u16 last_pidx, u8 len16)
{
	struct cstor_usock *ucsk = uqp->ucsk;
	struct fw_v2_nvmet_tx_data_wr *send_wr = &wqe->send_wr;
	u32 imm_plen = 0, isgl_plen = 0, crc_len = 0;
	u32 plen, flags;
	int ret;

	if (wr->sg_list[0].lkey) {
		ret = build_isgl((void *)(send_wr + 1), wr->sg_list, wr->num_sge, &isgl_plen);
		if (unlikely(ret)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "build_isgl() failed, "
				  "wr->num_sge %u ret %d\n", wr->num_sge, ret);
			return ret;
		}
	} else {
		u32 imm_size;
		u32 num_imm_sge;

		ret = build_immd(send_wr + 1, wr, &imm_plen, &num_imm_sge);
		if (unlikely(ret)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "build_immd() failed, "
				  "ret %d\n", ret);
			return ret;
		}

		imm_size = imm_plen;

		if (num_imm_sge < wr->num_sge) {
			ret = build_isgl((void *)(send_wr + 1) + imm_size,
					 wr->sg_list + num_imm_sge,
					 wr->num_sge - num_imm_sge, &isgl_plen);
			if (unlikely(ret)) {
				cstor_err(ucsk->ucdev, CSTOR_NOLOG, "build_isgl() failed, "
					  "imm_size %u num_imm_sge %u ret %d\n",
					  imm_size, num_imm_sge, ret);
				return ret;
			}
		}
	}

	send_wr->op_to_immdlen = htobe32(V_FW_WR_OP(FW_V2_NVMET_TX_DATA_WR) |
					 V_FW_WR_COMPL(is_completion_needed(uqp, wr->flags)) |
					 V_FW_WR_IMMDLEN(imm_plen));

	if (uqp->recv_bytes >= (256 * 1024 * 1024)) {
		send_wr->op_to_immdlen |= htobe32(F_FW_V2_NVMET_TX_DATA_WR_DACK_CHANGE |
						  V_FW_V2_NVMET_TX_DATA_WR_DACK_MODE(3));
		uqp->recv_bytes = 0;
	}

	send_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	send_wr->r4 = 0;
	send_wr->r5 = 0;
	send_wr->wrid = htobe16(last_pidx);
	send_wr->r6 = 0;

	switch (wr->opcode) {
	case CSTOR_SEND_OP_NVME_TCP_TX_PDU:
		flags = V_FW_V2_NVMET_TX_DATA_WR_FLAGS_HI(ULP_MODE_NVMET);
		break;
	case CSTOR_SEND_OP_ISCSI_TX_PDU:
		flags = V_FW_V2_NVMET_TX_DATA_WR_FLAGS_HI(ULP_MODE_ISCSI);
		break;
	default:
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "invalid opcode %u\n", wr->opcode);
		abort();
	}

	if (wr->flags & CSTOR_SEND_FLAG_HDGST) {
		crc_len = 4;
		flags |= F_FW_V2_NVMET_TX_DATA_WR_ULPSUBMODE_HCRC;
	}

	if (wr->flags & CSTOR_SEND_FLAG_DDGST) {
		crc_len += 4;
		flags |= F_FW_V2_NVMET_TX_DATA_WR_ULPSUBMODE_DCRC;
	}

	send_wr->flags_hi_to_flags_lo = htobe32(flags);

	plen = imm_plen + isgl_plen + crc_len + wr->nvme_tcp.num_pad_bytes;
	send_wr->plen = htobe32(plen);
	send_wr->seqno = htobe32(ucsk->snd_nxt);

	ucsk->snd_nxt += plen;

	return 0;
}

struct cstor_iso_info {
	u8 last;
	u8 hdgst;
	u8 ddgst;
	u8 pdo;
	u32 mpdu;
	u32 len;
};

#define cstor_ISO_FSLICE 0x1
#define cstor_ISO_LSLICE 0x2
static void
cstor_cpl_t7_tx_data_iso(struct cpl_t7_tx_data_iso *cpl, struct cstor_iso_info *iso_info)
{
	unsigned int fslice = 1;
	unsigned int lslice = iso_info->last;

	memset(cpl, 0, sizeof(*cpl));

	cpl->op_to_scsi = htobe32(V_CPL_T7_TX_DATA_ISO_OPCODE(CPL_TX_DATA_ISO) |
				  V_CPL_T7_TX_DATA_ISO_FIRST(fslice) |
				  V_CPL_T7_TX_DATA_ISO_LAST(lslice) |
				  V_CPL_T7_TX_DATA_ISO_CPLHDRLEN(0) |
				  V_CPL_T7_TX_DATA_ISO_HDRCRC(iso_info->hdgst) |
				  V_CPL_T7_TX_DATA_ISO_PLDCRC(iso_info->ddgst) |
				  V_CPL_T7_TX_DATA_ISO_IMMEDIATE(0) |
				  V_CPL_T7_TX_DATA_ISO_SCSI(2));
	cpl->nvme_tcp_pkd = F_CPL_T7_TX_DATA_ISO_NVME_TCP;
	cpl->mpdu = htobe16(DIV_ROUND_UP(iso_info->mpdu, 4));
	cpl->size = htobe32(iso_info->len);
	cpl->pdo_pkd = htobe32(iso_info->pdo);
}

static int
build_nvmet_tx_data_wr_lso(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
			   u16 last_pidx, u8 len16)
{
	struct cstor_usock *ucsk = uqp->ucsk;
	struct fw_v2_nvmet_tx_data_wr *send_wr = &wqe->send_wr;
	struct cpl_t7_tx_data_iso *cpl;
	struct cstor_iso_info iso_info = {};
	u32 imm_plen = 0, isgl_plen = 0, crc_len = 0;
	u32 plen, flags, num_pdu;
	u32 hdr_size;
	u32 mdsl = 8192;
	int ret;

	cpl = (void *)(send_wr + 1);

	if (wr->sg_list[0].lkey) {
		ret = build_isgl((void *)(cpl + 1), wr->sg_list, wr->num_sge, &isgl_plen);
		if (unlikely(ret)) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "build_isgl() failed, "
				  "wr->num_sge %u ret %d\n", wr->num_sge, ret);
			return ret;
		}

		hdr_size = 24;
		isgl_plen = isgl_plen - hdr_size;
	} else {
		u32 num_imm_sge;

		ret = build_immd(cpl + 1, wr, &imm_plen, &num_imm_sge);
		if (unlikely(ret)) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "build_immd() failed, ret %d\n", ret);
			return ret;
		}

		hdr_size = imm_plen;

		if (num_imm_sge < wr->num_sge) {
			ret = build_isgl((void *)(cpl + 1) + hdr_size, wr->sg_list + num_imm_sge,
					 wr->num_sge - num_imm_sge, &isgl_plen);
			if (unlikely(ret)) {
				cstor_err(uqp->ucdev, CSTOR_NOLOG, "build_isgl() failed, "
					  "wr->num_sge %u num_imm_sge %u ret %d\n",
					  wr->num_sge, num_imm_sge, ret);
				return ret;
			}
		}
	}

	send_wr->op_to_immdlen = htobe32(V_FW_WR_OP(FW_V2_NVMET_TX_DATA_WR) |
					 V_FW_WR_COMPL(is_completion_needed(uqp, wr->flags)) |
					 V_FW_WR_IMMDLEN(imm_plen));
	send_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	send_wr->r4 = 0;
	send_wr->r5 = 0;
	send_wr->wrid = htobe16(last_pidx);
	send_wr->r6 = 0;

	flags = V_FW_V2_NVMET_TX_DATA_WR_FLAGS_HI(ULP_MODE_NVMET);
	num_pdu = (isgl_plen + mdsl - 1) / mdsl;

	if (wr->flags & CSTOR_SEND_FLAG_HDGST) {
		crc_len = num_pdu * 4;
		iso_info.hdgst = 1;
		flags |= F_FW_V2_NVMET_TX_DATA_WR_ULPSUBMODE_HCRC;
	}

	if (wr->flags & CSTOR_SEND_FLAG_DDGST) {
		crc_len += (num_pdu * 4);
		iso_info.ddgst = 1;
		flags |= F_FW_V2_NVMET_TX_DATA_WR_ULPSUBMODE_DCRC;
	}

	flags |= F_FW_V2_NVMET_TX_DATA_WR_ULPSUBMODE_ISO;

	send_wr->flags_hi_to_flags_lo = htobe32(flags);

	plen = (hdr_size * num_pdu) + isgl_plen + crc_len + (wr->nvme_tcp.num_pad_bytes * num_pdu);
	send_wr->plen = htobe32(plen);
	send_wr->seqno = htobe32(ucsk->snd_nxt);

	ucsk->snd_nxt += plen;

	iso_info.mpdu = mdsl;
	iso_info.len = hdr_size + isgl_plen + wr->nvme_tcp.num_pad_bytes;
	iso_info.pdo = 24 + (iso_info.hdgst ? 4 : 0) + wr->nvme_tcp.num_pad_bytes;

	if (wr->flags & CSTOR_SEND_FLAG_LAST_PDU)
		iso_info.last = 1;

	cstor_cpl_t7_tx_data_iso(cpl, &iso_info);
	return 0;
}

static int
build_fr_nsmr_wr_dma_addr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
			  u16 last_pidx, u8 len16)
{
	struct cstor_usock *ucsk = uqp->ucsk;
	struct fw_nvmet_v2_fr_nsmr_wr *nsmr_wr = &wqe->nsmr_wr;
	struct fw_ri_tpte *tpte = (void *)(nsmr_wr + 1);
	struct cstor_sge *sge = wr->sg_list;
	__be64 *dma_addr;
	u64 first_page_offset;
	u64 addr;
	u32 length;
	u32 pbl_offset;
	u32 len = 0, i;
	bool non_contiguous;
	int ret;

	if (unlikely((wr->nvme_tcp.page_size < 4096) || (!IS_POWER_OF_2(wr->nvme_tcp.page_size)))) {
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "page size %llu not aligned to 4096\n",
			  wr->nvme_tcp.page_size);
		return EINVAL;
	}

	nsmr_wr->op_to_wrid = htobe32(V_FW_WR_OP(FW_NVMET_V2_FR_NSMR_WR) |
				      (is_completion_needed(uqp, 0) ? F_FW_WR_COMPL : 0) |
				      F_FW_NVMET_V2_FR_NSMR_WR_TPTE_PBL |
				      V_FW_NVMET_V2_FR_NSMR_WR_WRID(last_pidx));
	nsmr_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	nsmr_wr->r3 = 0;
	nsmr_wr->r4 = 0;
	nsmr_wr->mem_write_addr32 = htobe32(ucsk->ucdev->stag_start_addr32 + uqp->ddp.stag_idx +
					    (wr->nvme_tcp.ddp_tag >> 4));
	nsmr_wr->r5 = 0;
	nsmr_wr->imm_data_len32 = ((len16 << 4) - sizeof(*nsmr_wr)) >> 5;
	nsmr_wr->dsgl_data_len32 = 0;
	nsmr_wr->r6 = 0;

	tpte->valid_to_pdid = htobe32(F_FW_RI_TPTE_VALID | F_FW_RI_TPTE_STAGSTATE |
				      V_FW_RI_TPTE_STAGKEY((wr->nvme_tcp.ddp_tag & 0xf)) |
				      V_FW_RI_TPTE_STAGTYPE(FW_RI_STAG_NSMR) |
				      V_FW_RI_TPTE_PDID(uqp->qp.pd->pdid));
	tpte->locread_to_qpid = htobe32(V_FW_RI_TPTE_PERM(FW_RI_MEM_ACCESS_REM_WRITE) |
					V_FW_RI_TPTE_ADDRTYPE(FW_RI_ZERO_BASED_TO) |
					V_FW_RI_TPTE_PS(long_log2(wr->nvme_tcp.page_size) - 12) |
					V_FW_RI_TPTE_QPID(ucsk->csk.tid));

	pbl_offset = uqp->pbl_offset + ((wr->nvme_tcp.ddp_tag >> 4) * uqp->max_ddp_sge * 8);
	tpte->nosnoop_pbladdr = htobe32(V_FW_RI_TPTE_PBLADDR(pbl_offset >> 3));

	tpte->va_hi = htobe32(0);
	tpte->dca_mwbcnt_pstag = htobe32(0);
	tpte->len_hi = htobe32(wr->nvme_tcp.r2t_offset);

	dma_addr = (__be64 *)(tpte + 1);
	for (i = 0; i < wr->num_sge; i++) {
		if (unlikely(i && (i != (wr->num_sge - 1)) &&
			     (sge[i].length != wr->nvme_tcp.page_size))) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "sge idx %u, "
				  "length %u not equal to page size %llu\n",
				  i, sge[i].length, wr->nvme_tcp.page_size);
			return EINVAL;
		}

		ret = cstor_virt_to_dma_addr(uqp->ucdev, &sge[i], &addr, &length, &non_contiguous);
		if (unlikely(ret || non_contiguous)) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "failed to get dma address "
				  "for sge idx %u length %u ret %d non_contiguous %u\n",
				  i, sge[i].length, ret, non_contiguous);
			return non_contiguous ? EINVAL : ret;
		} else if (unlikely(i && (addr % wr->nvme_tcp.page_size))) {
			cstor_err(ucsk->ucdev, CSTOR_NOLOG, "dma address is not "
				  "multiple of page_size %llu for sge idx %u length %u\n",
				  wr->nvme_tcp.page_size, i, sge[i].length);
			return EINVAL;
		}

		if (!i) {
			first_page_offset = addr % wr->nvme_tcp.page_size;
			addr -= first_page_offset;
		}

		*dma_addr = htobe64(addr);
		dma_addr++;
		len += sge[i].length;
	}

	while (i % 4) {
		*dma_addr = htobe64(0);
		dma_addr++;
		i++;
	}

	tpte->va_lo_fbo = htobe32(first_page_offset);
	tpte->len_lo = htobe32(len);
	return 0;
}

static int
cstor_get_pbl_addr(struct cstor_usock *ucsk, struct cstor_send_wr *wr,
		   struct cstor_umr *umr, u32 *pbl_addr, u32 *fbo)
{
	struct cstor_sge *sge = wr->sg_list;
	u64 mr_off, pbl_idx;
	u64 pg_off;

	if (unlikely(sge->addr < umr->va_fbo)) {
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "invalid sge->addr(%llu) "
			  "< umr->va_fbo(%llu)\n", sge->addr, umr->va_fbo);
		return EINVAL;
	}

	if (unlikely((sge->addr + sge->length) > (umr->va_fbo + umr->len))) {
		cstor_err(ucsk->ucdev, CSTOR_NOLOG, "invalid sge->addr(%llu) "
			  "sge->length(%u) umr->va_fbo(%llu) umr->len(%llu)\n",
			  sge->addr, sge->length, umr->va_fbo, umr->len);
		return EINVAL;
	}

	pg_off = sge->addr & ~umr->page_mask;
	mr_off = sge->addr - umr->va_fbo;
	pbl_idx = ((umr->va_fbo & ~umr->page_mask) + mr_off) >> umr->page_shift;

	*pbl_addr = umr->pbl_addr + (pbl_idx * 8);
	*fbo = pg_off;

	return 0;
}

static int
build_fr_nsmr_wr_vaddr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
		       u16 last_pidx, u8 len16)
{
	struct cstor_usock *ucsk = uqp->ucsk;
	struct cstor_umr *umr;
	struct fw_nvmet_v2_fr_nsmr_wr *nsmr_wr = &wqe->nsmr_wr;
	struct fw_ri_tpte *tpte = (void *)(nsmr_wr + 1);
	struct cstor_sge *sge = wr->sg_list;
	u32 pbl_addr, fbo;
	int ret;

	umr = ucsk->ucdev->mmid2ptr[cstor_mmid(sge->lkey)];
	if (unlikely(!umr)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "umr unavailable\n");
		return EINVAL;
	}

	ret = cstor_get_pbl_addr(ucsk, wr, umr, &pbl_addr, &fbo);
	if (unlikely(ret)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_get_pbl_addr() failed, ret %d\n", ret);
		return ret;
	}

	nsmr_wr->op_to_wrid = htobe32(V_FW_WR_OP(FW_NVMET_V2_FR_NSMR_WR) |
				      (is_completion_needed(uqp, 0) ? F_FW_WR_COMPL : 0) |
				      V_FW_NVMET_V2_FR_NSMR_WR_WRID(last_pidx));
	nsmr_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	nsmr_wr->r3 = 0;
	nsmr_wr->r4 = 0;
	nsmr_wr->mem_write_addr32 = htobe32(ucsk->ucdev->stag_start_addr32 + uqp->ddp.stag_idx +
					    (wr->nvme_tcp.ddp_tag >> 4));
	nsmr_wr->r5 = 0;
	nsmr_wr->imm_data_len32 = sizeof(struct fw_ri_tpte) >> 5;
	nsmr_wr->dsgl_data_len32 = 0;
	nsmr_wr->r6 = 0;

	tpte->valid_to_pdid = htobe32(F_FW_RI_TPTE_VALID |
				      V_FW_RI_TPTE_STAGKEY((wr->nvme_tcp.ddp_tag & 0xf)) |
				      F_FW_RI_TPTE_STAGSTATE |
				      V_FW_RI_TPTE_STAGTYPE(FW_RI_STAG_NSMR) |
				      V_FW_RI_TPTE_PDID(umr->mr.pd->pdid));
	tpte->locread_to_qpid = htobe32(V_FW_RI_TPTE_PERM(FW_RI_MEM_ACCESS_REM_WRITE) |
					V_FW_RI_TPTE_ADDRTYPE(FW_RI_ZERO_BASED_TO) |
					V_FW_RI_TPTE_PS(umr->page_shift - 12) |
					V_FW_RI_TPTE_QPID(ucsk->csk.tid));
	tpte->nosnoop_pbladdr = htobe32(V_FW_RI_TPTE_PBLADDR((pbl_addr - umr->pbl_start) >> 3));
	tpte->len_lo = htobe32(sge->length);
	tpte->va_hi = htobe32(0);
	tpte->va_lo_fbo = htobe32(fbo);
	tpte->dca_mwbcnt_pstag = htobe32(0);
	tpte->len_hi = htobe32(wr->nvme_tcp.r2t_offset);

	return 0;
}

static int
build_fr_nsmr_wr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr, u16 last_pidx,
		 u8 len16)
{
	if (wr->num_sge > 1)
		return build_fr_nsmr_wr_dma_addr(uqp, wqe, wr, last_pidx, len16);
	else
		return build_fr_nsmr_wr_vaddr(uqp, wqe, wr, last_pidx, len16);
}

static int
build_inv_lstag_wr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
		   u16 last_pidx, u8 len16)
{
	struct fw_ri_inv_lstag_wr *inv_wr = &wqe->inv_wr;

	inv_wr->opcode = FW_RI_INV_LSTAG_WR;
	inv_wr->flags = FW_RI_COMPLETION_FLAG;
	inv_wr->wrid = htobe16(last_pidx);
	inv_wr->r1[0] = 0;
	inv_wr->r1[1] = 0;
	inv_wr->r1[2] = 0;

	inv_wr->len16 = len16;
	inv_wr->r2 = 0;
	inv_wr->stag_inv = htobe32(((uqp->ddp.stag_idx + (wr->nvme_tcp.ddp_tag >> 4)) << 8) |
				   (wr->nvme_tcp.ddp_tag & 0xf));
	uqp->pend_cmpl_ndesc = 0;

	return 0;
}

int build_recv_wr(union t4_recv_wr *wqe, struct cstor_recv_wr *wr, u16 wr_pidx, u8 len16)
{
	int ret;

	ret = build_isgl(&wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (unlikely(ret)) {
		cstor_printf(stderr, CSTOR_NOLOG, "build_isgl() failed, wr->num_sge %u ret %d\n",
			     wr->num_sge, ret);
		return ret;
	}

	wqe->recv.opcode = FW_RI_RECV_WR;
	wqe->recv.r1 = 0;
	wqe->recv.wrid = wr_pidx;
	wqe->recv.r2[0] = 0;
	wqe->recv.r2[1] = 0;
	wqe->recv.r2[2] = 0;
	wqe->recv.len16 = len16;

	return 0;
}

static u32 calc_num_imm_sge(struct cstor_send_wr *wr, u32 *imm_dsize)
{
	u32 i;

	for (i = 0; i < wr->num_sge; i++) {
		if (wr->sg_list[i].lkey)
			break;

		*imm_dsize += wr->sg_list[i].length;
	}

	return i;
}

static int
cstor_calc_wr_len(struct cstor_uqp *uqp, struct cstor_send_wr *wr, void *dsgl, u32 *wr_len)
{
	u32 ppod_idx;
	u32 num_ppods = 0;
	u32 imm_dsize = 0;
	u32 num_imm_sge = 0;
	int ret;

	*wr_len = 0;

	switch (wr->opcode) {
	case CSTOR_SEND_OP_NVME_TCP_SETUP_DDP:
		if (unlikely(!wr->num_sge || (wr->num_sge > uqp->max_ddp_sge) ||
			     (wr->flags & CSTOR_SEND_FLAG_CMPL))) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid wr->num_sge(%u) or "
				  "SEND_FLAG_CMPL is set, wr->flags %#x uqp->max_ddp_sge %u\n",
				  wr->num_sge, wr->flags, uqp->max_ddp_sge);
			return EINVAL;
		}
		*wr_len = sizeof(struct fw_nvmet_v2_fr_nsmr_wr) + sizeof(struct fw_ri_tpte);
		if (wr->num_sge > 1)
			*wr_len += (ROUND_UP(wr->num_sge, 4) * sizeof(__be64));

		break;
	case CSTOR_SEND_OP_NVME_TCP_INVALIDATE_TAG:
		*wr_len = sizeof(struct fw_ri_inv_lstag_wr);
		break;
	case CSTOR_SEND_OP_NVME_TCP_LSO:
		*wr_len = 32;
	case CSTOR_SEND_OP_NVME_TCP_TX_PDU:
		/* fallthrough */
	case CSTOR_SEND_OP_ISCSI_TX_PDU:
		if (unlikely(!wr->num_sge)) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid wr->num_sge is 0 "
				  "wr->opcode %u\n", wr->opcode);
			return EINVAL;
		}
		*wr_len += sizeof(struct fw_v2_nvmet_tx_data_wr);
		if (wr->sg_list[0].lkey) {
			*wr_len += sizeof(struct fw_ri_isgl) +
				   (wr->num_sge * sizeof(struct fw_ri_sge));
		} else {
			num_imm_sge = calc_num_imm_sge(wr, &imm_dsize);
			*wr_len += imm_dsize;
			if (num_imm_sge < wr->num_sge) {
				*wr_len += sizeof(struct fw_ri_isgl) +
					   ((wr->num_sge - num_imm_sge) * sizeof(struct fw_ri_sge));
			}
		}

		break;
	case CSTOR_SEND_OP_ISCSI_SETUP_DDP:
		if (unlikely((wr->num_sge > 1) || (wr->flags & CSTOR_SEND_FLAG_CMPL))) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid wr->num_sge %u > 1 or "
				  "SEND_FLAG_CMPL is set, wr->flags %#x\n", wr->num_sge, wr->flags);
			return EINVAL;
		}

		ppod_idx = (wr->iscsi.ddp_tag >> PPOD_IDX_SHIFT) - uqp->ucdev->iscsi_ppm->base_idx;
		num_ppods = (uqp->ucdev->iscsi_ppm->ppod_data + ppod_idx)->num_ppods;
		*wr_len = sizeof(struct fw_nvmet_v2_fr_nsmr_wr);
		if (unlikely(!num_ppods)) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid ddp wr, "
				  "opcode: %u num_ppods: %u\n", wr->opcode, num_ppods);
			return EINVAL;
		}

		if (num_ppods <= ULPMEM_IDATA_MAX_PPODS) {
			*wr_len += (num_ppods << PPOD_SIZE_SHIFT);
		} else {
			u32 num_dsgl_sge;

			ret = build_ulptx_sgl(uqp->ucdev, dsgl, wr->sg_list, &num_dsgl_sge);
			if (unlikely(ret)) {
				cstor_err(uqp->ucdev, CSTOR_NOLOG, "build_ulptx_sgl() failed, "
					  "ret %d\n", ret);
				return ret;
			}

			*wr_len += sizeof(struct ulptx_sgl) +
				   ((num_dsgl_sge - 1) * sizeof(struct ulptx_sge_pair));
		}
		break;
	case CSTOR_SEND_OP_ISCSI_INVALIDATE_TAG:
		*wr_len = sizeof(struct fw_nvmet_v2_fr_nsmr_wr);
		break;
	default:
		cstor_err(uqp->ucdev, CSTOR_LOG, "post of type=%u TBD!\n", wr->opcode);
		return EINVAL;
	}

	if (unlikely(*wr_len > T4_SQ_NUM_BYTES)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "unsupported wr_len %u wr->opcode %u wr->num_sge %u "
			  "num_imm_sge %u imm_dsize %u num_ppods %u\n",
			  *wr_len, wr->opcode, wr->num_sge, num_imm_sge, imm_dsize, num_ppods);
		return ENOTSUP;
	}

	return 0;
}

int cstor_post_send(struct cstor_qp *qp, struct cstor_send_wr *wr, struct cstor_send_wr **bad_wr)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_udevice *ucdev = uqp->ucdev;
	struct t4_swsqe *swsqe;
	union t4_wr lwqe, *wqe = NULL;
	struct cstor_send_wr *next_wr;
	u32 avail;
	u32 last_pidx;
	u32 wr_len;
	int ret;
	u16 idx = 0;
	u16 max_pidx = ucdev->plat_dev ? M_ARM_PIDX : M_PIDX_T5;
	u8 len16, ndesc;
	bool need_copy = false;
	u8 dsgl[sizeof(struct ulptx_sgl) + sizeof(struct ulptx_sge_pair)];

	if (unlikely(!bad_wr)) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid !bad_wr\n");
		return EINVAL;
	}

	if (unlikely(!uqp->mapped)) {
		cstor_err(ucdev, CSTOR_NOLOG, "qpid %u is not mapped\n", qp->qpid);
		return EINVAL;
	}

	cstor_spin_lock(&uqp->lock);
	avail = t4_sq_avail(&uqp->wq);
	if (unlikely(!avail)) {
		cstor_spin_unlock(&uqp->lock);
		*bad_wr = wr;
		return ENOMEM;
	}

	while (wr) {
		if (unlikely(t4_wq_in_error(&uqp->wq))) {
			cstor_err(ucdev, CSTOR_NOLOG, "QP is in error state\n");
			*bad_wr = wr;
			ret = ECONNRESET;
			break;
		}

		if (wr->flags & CSTOR_SEND_FLAG_CMPL) {
			if (unlikely(!wr->ctx || wr->cb_fn)) {
				cstor_err(ucdev, CSTOR_NOLOG, "SEND_FLAG_CMPL is set, "
					  "wr->ctx %p and wr->cb_fn %p\n", wr->ctx, wr->cb_fn);
				*bad_wr = wr;
				ret = EINVAL;
				break;
			}
		} else if (unlikely(!wr->cb_fn && wr->ctx)) {
			cstor_err(ucdev, CSTOR_NOLOG, "SEND_FLAG_CMPL is not set, "
				  "wr->cb_fn %p and wr->ctx %p\n", wr->cb_fn, wr->ctx);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		ret = cstor_calc_wr_len(uqp, wr, dsgl, &wr_len);
		if (unlikely(ret)) {
			cstor_err(ucdev, CSTOR_NOLOG, "cstor_calc_wr_len() failed, ret %d\n", ret);
			*bad_wr = wr;
			break;
		}

		len16 = DIV_ROUND_UP(wr_len, 16);
		ndesc = DIV_ROUND_UP(wr_len, T4_EQ_ENTRY_SIZE);

		if (unlikely(avail < ndesc)) {
			*bad_wr = wr;
			ret = ENOMEM;
			break;
		}

		uqp->pend_cmpl_ndesc += ndesc;

		last_pidx = uqp->wq.sq.pidx + ndesc - 1;
		if (last_pidx >= uqp->wq.sq.size) {
			last_pidx -= uqp->wq.sq.size;
			wqe = &lwqe;
			need_copy = true;
		} else {
			wqe = (union t4_wr *)((u8 *)uqp->wq.sq.queue +
			      (uqp->wq.sq.pidx * T4_EQ_ENTRY_SIZE));
		}

		swsqe = &uqp->wq.sq.sw_sq[last_pidx];

		switch (wr->opcode) {
		case CSTOR_SEND_OP_NVME_TCP_SETUP_DDP:
			ret = build_fr_nsmr_wr(uqp, wqe, wr, last_pidx, len16);
			break;
		case CSTOR_SEND_OP_NVME_TCP_INVALIDATE_TAG:
			ret = build_inv_lstag_wr(uqp, wqe, wr, last_pidx, len16);
			break;
		case CSTOR_SEND_OP_NVME_TCP_LSO:
			ret = build_nvmet_tx_data_wr_lso(uqp, wqe, wr, last_pidx, len16);
			break;
		case CSTOR_SEND_OP_NVME_TCP_TX_PDU:
			/* fallthrough */
		case CSTOR_SEND_OP_ISCSI_TX_PDU:
			ret = build_tx_data_wr(uqp, wqe, wr, last_pidx, len16);
			break;
		case CSTOR_SEND_OP_ISCSI_SETUP_DDP:
			ret = build_iscsi_ddp_wr(uqp, wqe, wr, dsgl, last_pidx, len16);
			break;
		case CSTOR_SEND_OP_ISCSI_INVALIDATE_TAG:
			ret = build_iscsi_invalidate_tag_wr(uqp, wqe, wr, dsgl, last_pidx, len16);
			break;
		}

		if (unlikely(ret)) {
			*bad_wr = wr;
			break;
		}

		if ((len16 << 4) - wr_len)
			memset((void *)wqe + wr_len, 0, (len16 << 4) - wr_len);

		swsqe->ctx = (wr->flags & CSTOR_SEND_FLAG_CMPL) ? wr->ctx : NULL;

		cstor_debug(ucdev, CSTOR_LOG, "wr->ctx %p pidx %#x wr->opcode %#x\n", wr->ctx,
			    uqp->wq.sq.pidx, wr->opcode);

		if (need_copy) {
			struct t4_wq *wq = &uqp->wq;
			void *dst = ((void *)wq->sq.queue + (wq->sq.pidx * T4_EQ_ENTRY_SIZE));
			void *queue_end = ((void *)wq->sq.queue +
					   (wq->sq.size * T4_EQ_ENTRY_SIZE));

			copy_wr_to_queue(dst, wqe, wq->sq.queue, queue_end, len16);
			need_copy = false;
		}

		t4_sq_produce(&uqp->wq, ndesc);

		if (unlikely((idx + ndesc) > max_pidx)) {
			t4_ring_sq_db(&uqp->wq, idx, len16, NULL, ucdev->wc_enabled,
				      ucdev->plat_dev);
			idx = ndesc;
		} else {
			idx += ndesc;
		}
		avail -= ndesc;

		/* wr->cb_fn() can free the wr, so save wr->next in next_wr */
		next_wr = wr->next;
		if (wr->cb_fn)
			wr->cb_fn(wr->ctx);
		wr = next_wr;
	}

	if (likely(idx))
		t4_ring_sq_db(&uqp->wq, idx, len16, wqe, ucdev->wc_enabled, ucdev->plat_dev);

	cstor_spin_unlock(&uqp->lock);
	return ret;
}

void cstor_hexdump(void *buf, u32 len)
{
	u32 *ptr = buf;
	u32 i, count;

	cstor_printf(stdout, CSTOR_NOLOG, "len %u\n", len);
	count = len >> 4;

	for (i = 0; i < count; i++) {
		cstor_printf(stdout, CSTOR_NOLOG, "%p: %08x %08x %08x %08x\n",
			     ptr, ntohl(ptr[0]), ntohl(ptr[1]), ntohl(ptr[2]), ntohl(ptr[3]));
		ptr += 4;
	}

	if ((len % 16) == 8)
		cstor_printf(stdout, CSTOR_NOLOG, "%p: %08x %08x\n", ptr,
			     ntohl(ptr[0]), ntohl(ptr[1]));
}

int cstor_post_rq_recv(struct cstor_qp *qp, struct cstor_recv_wr *wr, struct cstor_recv_wr **bad_wr)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_udevice *ucdev = uqp->ucdev;
	union t4_recv_wr *wqe, lwqe;
	bool need_copy = false;
	u32 num_wrs, ndesc;
	int ret;
	u16 idx = 0;
	u16 max_pidx = ucdev->plat_dev ? M_ARM_PIDX : M_PIDX_T5;
	u8 len16;

	if (unlikely(!bad_wr)) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid bad_wr is NULL\n");
		return EINVAL;
	}

	if (unlikely(!uqp->mapped)) {
		cstor_err(ucdev, CSTOR_NOLOG, "qpid %u is not mapped\n", qp->qpid);
		return EINVAL;
	}

	cstor_spin_lock(&uqp->lock);
	num_wrs = t4_rq_avail(&uqp->wq);
	while (wr) {
		if (unlikely(t4_wq_in_error(&uqp->wq))) {
			cstor_err(ucdev, CSTOR_NOLOG, "QP is in error state\n");
			*bad_wr = wr;
			ret = ECONNRESET;
			break;
		}

		if (unlikely(!num_wrs)) {
			cstor_debug(ucdev, CSTOR_NOLOG,
				    "No slots! wr_pidx: %u wr_cidx: %u in_use: %u "
				    "max_wr: %u size: %u\n",
				    uqp->wq.rq.wr_pidx, uqp->wq.rq.wr_cidx, uqp->wq.rq.in_use,
				    uqp->wq.rq.max_wr, uqp->wq.rq.size);
			*bad_wr = wr;
			ret = ENOMEM;
			break;
		}
		if (unlikely(wr->num_sge > T4_MAX_RQ_SGE)) {
			cstor_err(ucdev, CSTOR_NOLOG, "invalid wr->num_sge(%u) > "
				  "T4_MAX_RQ_SGE(%u)\n", wr->num_sge, T4_MAX_RQ_SGE);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		len16 = DIV_ROUND_UP(sizeof(wqe->recv) +
				     (wr->num_sge * sizeof(struct fw_ri_sge)), 16);
		ndesc = DIV_ROUND_UP(len16 << 4, T4_EQ_ENTRY_SIZE);

		if (((u32)(uqp->wq.rq.pidx) + ndesc) > (u32)(uqp->wq.rq.size)) {
			wqe = &lwqe;
			need_copy = true;
		} else {
			wqe = ((void *)uqp->wq.rq.queue + (uqp->wq.rq.pidx * T4_EQ_ENTRY_SIZE));
		}

		ret = build_recv_wr(wqe, wr, uqp->wq.rq.wr_pidx, len16);
		if (unlikely(ret)) {
			cstor_err(ucdev, CSTOR_NOLOG, "build_recv_wr() failed ret %d\n", ret);
			*bad_wr = wr;
			break;
		}

		uqp->wq.rq.sw_rq[uqp->wq.rq.wr_pidx].ctx = wr->ctx;
		uqp->wq.rq.sw_rq[uqp->wq.rq.wr_pidx].hdr = (void *)wr->sg_list->addr;

		cstor_debug(ucdev, CSTOR_LOG, "wr->ctx %p pidx %u\n",
			    wr->ctx, uqp->wq.rq.wr_pidx);

		if (need_copy) {
			struct t4_wq *wq = &uqp->wq;
			void *src = wqe;
			void *dst = ((void *)wq->rq.queue + (wq->rq.pidx * T4_EQ_ENTRY_SIZE));

			copy_wr_to_queue(dst, src, wq->rq.queue, &wq->rq.queue[wq->rq.max_wr],
					 len16);
			need_copy = false;
		}

		t4_rq_produce(&uqp->wq, ndesc);

		if (unlikely((idx + ndesc) > max_pidx)) {
			t4_ring_rq_db(&uqp->wq, idx, len16, NULL, ucdev->plat_dev);
			idx = ndesc;
		} else {
			idx += ndesc;
		}
		num_wrs--;
		wr = wr->next;
	}

	if (likely(idx))
		t4_ring_rq_db(&uqp->wq, idx, len16, wqe, ucdev->plat_dev);

	cstor_spin_unlock(&uqp->lock);
	return ret;
}

int cstor_alloc_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 *tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_ddp_tag *ddp = &uqp->ddp;
	u32 bits_in_long = sizeof(unsigned long) * 8;
	u32 rem_tags = ddp->num_tags % bits_in_long;
	u32 i;
	int idx = -1;
	u16 ddp_tag;
	u8 color;

	cstor_spin_lock(&ddp->lock);
	for (i = 0; i < ddp->num_long; i++) {
		idx = ffsl(ddp->tag_bm[i]);
		if (idx > 0) {
			if (rem_tags && (i == (ddp->num_long - 1)) && (idx > rem_tags))
				idx = -1;
			else
				ddp->tag_bm[i] &= ~(1UL << (idx - 1));

			break;
		}
	}
	cstor_spin_unlock(&ddp->lock);

	if (idx <= 0)
		return ENOMEM;

	ddp_tag = (i * bits_in_long) + (idx - 1);

	color = ddp->color[ddp_tag]++;
	if (ddp->color[ddp_tag] >= 16)
		ddp->color[ddp_tag] = 1;

	ddp_tag = (ddp_tag << 4) | color;
	*tag = ddp_tag;

	return 0;
}

int cstor_realloc_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 *tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_ddp_tag *ddp = &uqp->ddp;
	u16 ddp_tag = *tag >> 4;
	u8 color;

	if (unlikely(ddp_tag >= ddp->num_tags)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid ddp_tag %u >= ddp->num_long %u\n",
			  ddp_tag, ddp->num_long);
		return EINVAL;
	}

	color = ddp->color[ddp_tag]++;
	if (ddp->color[ddp_tag] >= 16)
		ddp->color[ddp_tag] = 1;

	ddp_tag = (ddp_tag << 4) | color;
	*tag = ddp_tag;
	return 0;
}

int cstor_free_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_ddp_tag *ddp = &uqp->ddp;
	u32 bits_in_long = sizeof(unsigned long) * 8;
	u32 i, idx;
	u16 ddp_tag = tag >> 4;

	i = ddp_tag / bits_in_long;
	if (unlikely(i >= ddp->num_long)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid i(%u) >= "
			  "ddp->num_long(%u)\n", i, ddp->num_long);
		return EINVAL;
	}

	idx = ddp_tag % bits_in_long;

	cstor_spin_lock(&ddp->lock);
	ddp->tag_bm[i] |= (1UL << idx);
	cstor_spin_unlock(&ddp->lock);

	return 0;
}

int cstor_send_iscsi_pdu(struct cstor_sock *csk, void *pdu, u32 len, u8 hdgst, u8 ddgst)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_send_iscsi_pdu_cmd cmd = {};
	int ret;

	cmd.buf = (uintptr_t)pdu;
	cmd.buf_len = len;
	cmd.tid = csk->tid;
	cmd.hdgst = hdgst;
	cmd.ddgst = ddgst;

	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u buf %llu buf_len %u hdgst %u ddgst %u\n",
		    cmd.tid, cmd.buf, cmd.buf_len, cmd.hdgst, cmd.ddgst);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SEND_ISCSI_PDU, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SEND_ISCSI_PDU cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}
