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

struct cstor_mr *
__cstor_reg_mr(struct cstor_sock *csk, struct cstor_pd *pd, void *addr, u64 length,
	       enum cstor_access_flags access)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(pd->cdev);
	struct cstor_umr *umr;
	struct cstor_reg_mr_cmd cmd = {};
	u32 pbl_depth = length / cstor_page_size;
	u32 size;
	int ret;

	if (length % cstor_page_size)
		pbl_depth++;

	if (((u64)(uintptr_t)addr) & (cstor_page_size - 1))
		pbl_depth++;

	size = sizeof(*umr) + (pbl_depth * sizeof(uint64_t));
	umr = malloc(size);
	if (!umr) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate umr, size %u\n", size);
		errno = ENOMEM;
		return NULL;
	}

	cmd.start = (uintptr_t)addr;
	cmd.length = length;
	cmd.pbl_ptr = (u64)(uintptr_t)umr->sw_pbl;

	if (access & CSTOR_ACCESS_LOCAL_WRITE)
		cmd.acc |= _CSTOR_ACCESS_LOCAL_WRITE;

	if (access & CSTOR_ACCESS_REMOTE_WRITE)
		cmd.acc |= _CSTOR_ACCESS_REMOTE_WRITE;

	if (access & CSTOR_ACCESS_REMOTE_READ)
		cmd.acc |= _CSTOR_ACCESS_REMOTE_READ;

	if (csk) {
		cmd.tid = csk->tid;
		cstor_debug(ucdev, CSTOR_NOLOG, "cmd.tid %u\n", cmd.tid);
	} else {
		cmd.srq = 1;
		cstor_debug(ucdev, CSTOR_NOLOG, "cmd.srq %u\n", cmd.srq);
	}

	cmd.pdid = pd->pdid;

	cstor_debug(ucdev, CSTOR_NOLOG, "pbl_depth %u length %llu cstor_page_size %llu addr %llu\n",
		    pbl_depth, length, cstor_page_size, cmd.start);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_REG_MR, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_REG_MR cmd, "
			  "ret %d\n", ret);
		free(umr);
		errno = ret;
		return NULL;
	}

	umr->mr.addr = addr;
	umr->mr.length = length;
	umr->mr.lkey = cmd.resp.lkey;
	umr->pbl_addr = cmd.resp.pbl_addr;
	umr->page_size = cmd.resp.page_size;
	umr->page_mask = ~(umr->page_size - 1);
	umr->page_shift = long_log2(umr->page_size);
	umr->va_fbo = (uintptr_t)addr;
	umr->len = length;
	umr->mr.pd = pd;
	umr->pbl_start = cmd.resp.pbl_start;

	cstor_debug(ucdev, CSTOR_LOG, "va_fbo 0x%llx len %llu\n", umr->va_fbo, umr->len);

	pthread_spin_lock(&ucdev->lock);
	ucdev->mmid2ptr[cstor_mmid(umr->mr.lkey)] = umr;
	pthread_spin_unlock(&ucdev->lock);

	return &umr->mr;
}

struct cstor_mr *
cstor_reg_mr(struct cstor_pd *pd, void *addr, u64 length, enum cstor_access_flags access)
{
	return __cstor_reg_mr(NULL, pd, addr, length, access);
}

int cstor_dereg_mr(struct cstor_mr *mr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(mr->pd->cdev);
	struct cstor_dereg_mr_cmd cmd = {};
	int ret;

	cmd.lkey = mr->lkey;
	cstor_debug(ucdev, CSTOR_NOLOG, "lkey %u\n", cmd.lkey);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DEREG_MR, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DEREG_MR cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	pthread_spin_lock(&ucdev->lock);
	ucdev->mmid2ptr[cstor_mmid(mr->lkey)] = NULL;
	pthread_spin_unlock(&ucdev->lock);

	free(to_cstor_umr(mr));
	return 0;
}

int cstor_set_iscsi_region_status(struct cstor_udevice *ucdev, u8 status)
{
	struct cstor_set_iscsi_region_status_cmd cmd = {};
	int ret;

	cmd.status = status;
	cstor_debug(ucdev, CSTOR_NOLOG, "status %u\n", status);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SET_ISCSI_REGION_STATUS, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl "
			  "CSTOR_IOCTL_SET_ISCSI_REGION_STATUS cmd, ret %d\n", ret);
		return ret;
	}

	return 0;
}
