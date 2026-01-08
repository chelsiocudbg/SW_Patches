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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <assert.h>

#include "cstor_umain.h"
#include "cstor_ioctl.h"

static int __cstor_destroy_srq(struct cstor_udevice *ucdev, u32 srqid)
{
	struct cstor_destroy_srq_cmd cmd = {};
	int ret;

	cmd.srqid = srqid;

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DESTROY_SRQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DESTROY_SRQ cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

struct cstor_srq *cstor_create_srq(struct cstor_pd *pd, struct cstor_srq_attr *attr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(pd->cdev);
	struct cstor_usrq *usrq;
	struct cstor_create_srq_cmd cmd = {};
	u64 segment_offset;
	int ret;

	usrq = calloc(1, sizeof(*usrq));
	if (!usrq) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate usrq\n");
		ret = ENOMEM;
		goto err;
	}

	cmd.max_wr = attr->max_wr;
	cmd.pdid = pd->pdid;

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_CREATE_SRQ, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_SRQ cmd, "
			  "ret %d\n", ret);
		goto err_free_srq_mem;
	}

	cstor_debug(ucdev, CSTOR_LOG, "usrq id %#x usrq key %llu usrq db/gts key %llu "
		    "qid_mask %#x\n", cmd.resp.srqid, cmd.resp.srq_key,
		    cmd.resp.srq_db_gts_key, cmd.resp.qid_mask);

	usrq->srq.pd = pd;
	usrq->srq.srqid = cmd.resp.srqid;
	usrq->ucdev = ucdev;
	usrq->wq.qid = cmd.resp.srqid;
	usrq->wq.size = cmd.resp.srq_size;
	usrq->wq.max_wr = cmd.resp.srq_max_wr;
	usrq->wq.memsize = cmd.resp.srq_memsize;
	usrq->wq.rqt_abs_idx = cmd.resp.rqt_abs_idx;
	usrq->flags = cmd.resp.flags;
	cstor_spin_init(&usrq->lock, attr->no_lock);

	usrq->wq.udb = mmap(NULL, cstor_page_size, PROT_WRITE, MAP_SHARED,
			    ucdev->cdev.dev_fd, cmd.resp.srq_db_gts_key);
	if (usrq->wq.udb == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, cstor_page_size %llu "
			  "cmd.resp.srq_db_gts_key %llu\n", cstor_page_size,
			  cmd.resp.srq_db_gts_key);
		goto err_destroy_srq;
	}

	if (!ucdev->plat_dev) {
		segment_offset = 128 * (usrq->wq.qid & cmd.resp.qid_mask);
		if (segment_offset < cstor_page_size) {
			usrq->wq.udb += segment_offset / 4;
			usrq->wq.wc_reg_available = true;
		} else {
			usrq->wq.bar2_qid = usrq->wq.qid & cmd.resp.qid_mask;
		}

		usrq->wq.udb += 2;
	}

	usrq->wq.queue = mmap(NULL, usrq->wq.memsize, PROT_WRITE, MAP_SHARED,
			      ucdev->cdev.dev_fd, cmd.resp.srq_key);
	if (usrq->wq.queue == MAP_FAILED) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "mmap() failed, usrq->wq.memsize %lu "
			  "cmd.resp.srq_key %llu\n", usrq->wq.memsize, cmd.resp.srq_key);
		goto err_unmap_udb;
	}

	usrq->mapped = true;

	usrq->wq.sw_rq = calloc(usrq->wq.max_wr, sizeof(struct t4_swrqe));
	if (!usrq->wq.sw_rq) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate usrq->wq.sw_rq\n");
		ret = ENOMEM;
		goto err_unmap_queue;
	}

	cstor_debug(ucdev, CSTOR_LOG, "usrq dbva %p usrq qva %p usrq depth %u usrq memsize %lu\n",
		    usrq->wq.udb, usrq->wq.queue, usrq->wq.max_wr, usrq->wq.memsize);

	return &usrq->srq;

err_unmap_queue:
	munmap(usrq->wq.queue, usrq->wq.memsize);
err_unmap_udb:
	munmap(MASKED(usrq->wq.udb), cstor_page_size);
err_destroy_srq:
	__cstor_destroy_srq(ucdev, usrq->wq.qid);
err_free_srq_mem:
	free(usrq);
err:
	errno = ret;
	return NULL;
}

int cstor_destroy_srq(struct cstor_srq *srq)
{
	struct cstor_usrq *usrq = to_cstor_usrq(srq);
	struct cstor_udevice *ucdev = usrq->ucdev;
	int ret;

	cstor_debug(ucdev, CSTOR_LOG, "srq qid %u\n", usrq->wq.qid);

	if (usrq->mapped) {
		munmap(MASKED(usrq->wq.udb), cstor_page_size);
		munmap(usrq->wq.queue, usrq->wq.memsize);
		usrq->mapped = false;
	}

	ret = __cstor_destroy_srq(ucdev, usrq->wq.qid);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, " __cstor_destroy_srq() failed, "
			  "qid %u\n", usrq->wq.qid);
		return ret;
	}

	free(usrq->wq.sw_rq);
	free(usrq);
	return 0;
}

int
cstor_post_srq_recv(struct cstor_srq *srq, struct cstor_recv_wr *wr, struct cstor_recv_wr **bad_wr)
{
	struct cstor_usrq *usrq = to_cstor_usrq(srq);
	struct cstor_udevice *ucdev = usrq->ucdev;
	struct t4_srq *wq = &usrq->wq;
	union t4_recv_wr *wqe, lwqe;
	int ret;
	u16 num_wrs;
	u16 idx = 0;
	u16 max_pidx = ucdev->plat_dev ? M_ARM_PIDX : M_PIDX_T5;
	u8 len16;
	u8 ndesc;
	bool need_copy = false;

	if (unlikely(!bad_wr)) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid bad_wr: NULL\n");
		return EINVAL;
	}

	if (unlikely(!usrq->mapped)) {
		cstor_err(ucdev, CSTOR_NOLOG, "srqid %u is not mapped\n", srq->srqid);
		return EINVAL;
	}

	cstor_spin_lock(&usrq->lock);
	num_wrs = t4_srq_avail(wq);
	while (wr) {
		if (unlikely(!num_wrs)) {
			cstor_debug(ucdev, CSTOR_NOLOG, "No space! "
				    "wq->wr_pidx %u wq->wr_cidx %u wq->max_wr %u wq->size %u "
				    "wq->in_use %u\n", wq->wr_pidx, wq->wr_cidx, wq->max_wr,
				    wq->size, wq->in_use);
			*bad_wr = wr;
			ret = ENOMEM;
			break;
		}

		if (unlikely(wr->num_sge > T4_MAX_RQ_SGE)) {
			cstor_err(ucdev, CSTOR_NOLOG, "invalid wr->num_sge %u > "
				  "T4_MAX_RQ_SGE %u\n", wr->num_sge, T4_MAX_RQ_SGE);
			*bad_wr = wr;
			ret = EINVAL;
			break;
		}

		len16 = DIV_ROUND_UP(sizeof(wqe->recv) +
				     wr->num_sge * sizeof(struct fw_ri_sge), 16);
		ndesc = DIV_ROUND_UP(len16 << 4, T4_EQ_ENTRY_SIZE);

		if (((u32)(wq->pidx) + ndesc) > (u32)(wq->size)) {
			wqe = &lwqe;
			need_copy = true;
		} else {
			wqe = ((void *)wq->queue + (wq->pidx * T4_EQ_ENTRY_SIZE));
		}

		ret = build_recv_wr(wqe, wr, wq->wr_pidx, len16);
		if (unlikely(ret)) {
			cstor_err(ucdev, CSTOR_NOLOG,
				  "build_recv_wr() failed, ret %d\n", ret);
			*bad_wr = wr;
			break;
		}

		wq->sw_rq[wq->wr_pidx].ctx = wr->ctx;
		wq->sw_rq[wq->wr_pidx].valid = true;
		cstor_debug(ucdev, CSTOR_LOG,
			    "wr_cidx %u wr_pidx %u pidx %u in_use %u ctx %p\n",
			    wq->wr_cidx, wq->wr_pidx, wq->pidx, wq->in_use, wr->ctx);

		if (need_copy) {
			copy_wr_to_queue(((void *)wq->queue + (wq->pidx * T4_EQ_ENTRY_SIZE)), wqe,
					 wq->queue, &wq->queue[wq->max_wr], len16);
			need_copy = false;
		}

		t4_srq_produce(wq, ndesc);

		if (unlikely((idx + ndesc) > max_pidx)) {
			t4_ring_srq_db(wq, idx, len16, NULL, ucdev->plat_dev);
			idx = ndesc;
		} else {
			idx += ndesc;
		}
		num_wrs--;
		wr = wr->next;
	}

	if (idx)
		t4_ring_srq_db(wq, idx, len16, wqe, ucdev->plat_dev);
	cstor_spin_unlock(&usrq->lock);

	return ret;
}
