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
#ifndef __CSTOR_UMAIN_H__
#define __CSTOR_UMAIN_H__

#include <pthread.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <ccan/list.h>
#include "cstor_udefs.h"
#include "cstor_ioctl.h"
#include "libcstor.h"
#include "cstor_uiscsi_ddp.h"

extern u64 cstor_page_size;
extern u64 cstor_page_mask;

extern struct list_head devices;

#define MASKED(x) ((void *)((unsigned long)(x) & cstor_page_mask))

struct cstor_spin_lock {
	pthread_spinlock_t lock;
	bool no_lock;
	bool locked;
};

struct cstor_udevice {
	struct cstor_device cdev;
	struct cstor_ulisten_sock **stid2ptr;
	struct cstor_usock **tid2ptr;
	struct cstor_usock **atid2ptr;
	struct cstor_umr **mmid2ptr;
	struct cstor_ppm *iscsi_ppm;
	struct list_node list;
	pthread_spinlock_t lock;
	u32 stid_base;
	u32 tid_base;
	u32 ref_count;
	u32 max_pdu_size;
	u32 stag_start_addr32;
	u32 iscsi_region_size;
	u32 iscsi_tagmask;
	u32 ppod_llimit;
	u32 ppod_start;
	u32 edram_start;
	u32 edram_size;
	u32 edram_ppod_zone_percentage[MAX_EDRAM_ZONES];
	u32 edram_ppod_per_bit[MAX_EDRAM_ZONES];
	u32 ddr_ppod_zone_percentage[MAX_DDR_ZONES];
	u32 ddr_ppod_per_bit[MAX_DDR_ZONES];
	u8 num_edram_zones;
	u8 num_ddr_zones;
	u8 plat_dev;
	bool wc_enabled;
};

struct cstor_upd {
	struct cstor_pd pd;
};

struct cstor_umr {
	struct cstor_mr mr;
	u64 va_fbo;
	u64 len;
	u64 page_size;
	u64 page_shift;
	u64 page_mask;
	u32 pbl_addr;
	u32 pbl_start;
	u64 sw_pbl[];
};

static inline u32 cstor_mmid(u32 stag)
{
	return (stag >> 8);
}

struct cstor_ucq {
	struct cstor_cq cq;
	struct cstor_udevice *ucdev;
	struct t4_cq q;
	struct cstor_spin_lock lock;
	bool mapped;
};

struct cstor_ddp_tag {
	u64 *tag_bm;
	u8 *color;
	u16 num_long;
	u16 num_tags;
	u32 stag_idx;
	struct cstor_spin_lock lock;
};

struct cstor_uqp {
	struct cstor_qp qp;
	struct cstor_udevice *ucdev;
	struct t4_wq wq;
	struct cstor_spin_lock lock;
	struct cstor_usrq *usrq;
	struct cstor_urxq *urxq;
	struct cstor_usock *ucsk;
	struct cstor_ddp_tag ddp;
	u32 pbl_offset;
	u32 max_ddp_sge;
	u32 iscsi_ddp_page_size;
	u16 pend_cmpl_ndesc;
	bool auto_cmpl;
	bool mapped;
};

struct cstor_usrq {
	struct cstor_srq srq;
	struct cstor_udevice *ucdev;
	struct t4_srq wq;
	struct cstor_spin_lock lock;
	__u32 flags;
	bool mapped;
};

struct cstor_urxq {
	struct cstor_rxq rxq;
	struct cstor_udevice *ucdev;
	struct t4_iq iq;
	struct t4_fl fl;
	struct cstor_spin_lock lock;
	struct cstor_iscsi_wc defer_wc;
	int qid_mask;
	bool mapped;
};

#define CSTOR_LCSK_INADDR_ANY_PORT_ID 0xFF
struct cstor_ulisten_sock {
	struct cstor_listen_sock lcsk[CSTOR_MAX_PORTS];
	struct cstor_udevice *ucdev;
	u32 num_sock;
	u8 refcnt;
};

struct cstor_usock {
	struct cstor_sock csk;
	struct cstor_udevice *ucdev;
	struct cstor_uqp *uqp;
};

enum fl_page_size_cap_order {
	CSTOR_FL_PAGE_SIZE_4K	= 0x0U,
	CSTOR_FL_PAGE_SIZE_16K	= 0x8U,
	CSTOR_FL_PAGE_SIZE_64K	= 0x1U,
};

enum cstor_send_err {
	CSTOR_SEND_SUCCESS				= 0x00,
	CSTOR_SEND_ERR_INVALID_STAG			= 0x01,
	CSTOR_SEND_ERR_PDID_MISMATCH			= 0x02,
	CSTOR_SEND_ERR_QPID_MISMATCH			= 0x03,
	CSTOR_SEND_ERR_ACCESS				= 0x04,
	CSTOR_SEND_ERR_WRAP				= 0x05,
	CSTOR_SEND_ERR_BOUND				= 0x06,
	CSTOR_SEND_ERR_INVALIDATE_SHARED_MR		= 0x07,
	CSTOR_SEND_ERR_INVALIDATE_MR_WITH_MW_BOUND	= 0x08,
	CSTOR_SEND_ERR_ECC				= 0x09,
	CSTOR_SEND_ERR_ECC_PSTAG			= 0x0A,
	CSTOR_SEND_ERR_PBL_ADDR_BOUND			= 0x0B,
	CSTOR_SEND_ERR_CRC				= 0x10,
	CSTOR_SEND_ERR_MARKER				= 0x11,
	CSTOR_SEND_ERR_PDU_LEN_ERR			= 0x12,
	CSTOR_SEND_ERR_OUT_OF_RQE			= 0x13,
	CSTOR_SEND_ERR_DDP_VERSION			= 0x14,
	CSTOR_SEND_ERR_RDMA_VERSION			= 0x15,
	CSTOR_SEND_ERR_RDMA_OPCODE			= 0x16,
	CSTOR_SEND_ERR_DDP_QUEUE_NUM			= 0x17,
	CSTOR_SEND_ERR_MSN				= 0x18,
	CSTOR_SEND_ERR_TBIT				= 0x19,
	CSTOR_SEND_ERR_MO				= 0x1A,
	CSTOR_SEND_ERR_RQE_ADDR_BOUND			= 0x1E,
	CSTOR_SEND_ERR_INTERNAL				= 0x1F,

	CSTOR_SEND_ERR_MAX,
};

struct cstor_uevent_channel {
	struct cstor_event_channel event_channel;
};

static inline struct cstor_urxq *to_cstor_urxq(struct cstor_rxq *rxq)
{
	return container_of(rxq, struct cstor_urxq, rxq);
}

static inline struct cstor_usrq *to_cstor_usrq(struct cstor_srq *srq)
{
	return container_of(srq, struct cstor_usrq, srq);
}

static inline struct cstor_udevice *to_cstor_udevice(struct cstor_device *cdev)
{
	return container_of(cdev, struct cstor_udevice, cdev);
}

static inline struct cstor_upd *to_cstor_upd(struct cstor_pd *pd)
{
	return container_of(pd, struct cstor_upd, pd);
}

static inline struct cstor_ucq *to_cstor_ucq(struct cstor_cq *cq)
{
	return container_of(cq, struct cstor_ucq, cq);
}

static inline struct cstor_uqp *to_cstor_uqp(struct cstor_qp *qp)
{
	return container_of(qp, struct cstor_uqp, qp);
}

static inline struct cstor_umr *to_cstor_umr(struct cstor_mr *mr)
{
	return container_of(mr, struct cstor_umr, mr);
}

static inline struct cstor_ulisten_sock *to_cstor_ulisten_sock(struct cstor_listen_sock *lcsk)
{
	u8 port_id = (lcsk->port_id == CSTOR_LCSK_INADDR_ANY_PORT_ID) ? 0 : lcsk->port_id;

	return container_of(lcsk, struct cstor_ulisten_sock, lcsk[port_id]);
}

static inline struct cstor_usock *to_cstor_usock(struct cstor_sock *csk)
{
	return container_of(csk, struct cstor_usock, csk);
}

static inline struct cstor_uevent_channel *
to_cstor_uevent_channel(struct cstor_event_channel *event_channel)
{
	return container_of(event_channel, struct cstor_uevent_channel, event_channel);
}

static inline void
set_listen_sock(struct cstor_udevice *ucdev, u32 stid, struct cstor_ulisten_sock *ulcsk)
{
	ucdev->stid2ptr[stid - ucdev->stid_base] = ulcsk;
}

static inline struct cstor_ulisten_sock *get_listen_sock(struct cstor_udevice *ucdev, u32 stid)
{
	return ucdev->stid2ptr[stid - ucdev->stid_base];
}

static inline void
set_sock(struct cstor_udevice *ucdev, u32 tid, struct cstor_usock *ucsk)
{
	ucdev->tid2ptr[tid - ucdev->tid_base] = ucsk;
}

static inline struct cstor_usock *get_sock(struct cstor_udevice *ucdev, u32 tid)
{
	return ucdev->tid2ptr[tid - ucdev->tid_base];
}

static inline unsigned int long_log2(unsigned long x)
{
	unsigned int r = 0;

	for (x >>= 1; x > 0; x >>= 1)
		r++;

	return r;
}

static inline void cstor_spin_init(struct cstor_spin_lock *lock, bool no_lock)
{
	if (no_lock)
		lock->locked = false;
	else
		pthread_spin_init(&lock->lock, PTHREAD_PROCESS_PRIVATE);

	lock->no_lock = no_lock;
}

static inline void cstor_spin_lock(struct cstor_spin_lock *lock)
{
	if (lock->no_lock) {
		if (lock->locked)
			abort();
		lock->locked = true;
	} else {
		pthread_spin_lock(&lock->lock);
	}
}

static inline void cstor_spin_unlock(struct cstor_spin_lock *lock)
{
	if (lock->no_lock) {
		if (!lock->locked)
			abort();
		lock->locked = false;
	} else {
		pthread_spin_unlock(&lock->lock);
	}
}

static inline int cstor_ioctl(int dev_fd, unsigned long cmd, void *buf)
{
	if (ioctl(dev_fd, cmd, buf))
		return errno;

	return 0;
}

int
cstor_process_connect_req_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				struct cstor_event *evt);
int
cstor_process_connect_rpl_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				struct cstor_event *evt);
int
cstor_process_disconnected_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				 struct cstor_event *evt);
int
cstor_virt_to_dma_addr(struct cstor_udevice *ucdev, struct cstor_sge *sge,
		       u64 *dma_addr, u32 *length, bool *non_contiguous);
void copy_wr_to_queue(void *dst, void *src, void *queue_start, void *queue_end, u8 len16);
int build_recv_wr(union t4_recv_wr *wqe, struct cstor_recv_wr *wr, u16 wr_pidx, u8 len16);
int cstor_set_iscsi_region_status(struct cstor_udevice *ucdev, u8 status);
u32 cstor_fls(u32 val);

static inline u8 is_completion_needed(struct cstor_uqp *uqp, u8 flags)
{
	if ((flags & CSTOR_SEND_FLAG_CMPL) ||
	    (uqp->auto_cmpl && (uqp->pend_cmpl_ndesc >= ((uqp->wq.sq.size - 1) / 2)))) {
		uqp->pend_cmpl_ndesc = 0;
		return 1;
	}

	return 0;
}

static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

#endif
