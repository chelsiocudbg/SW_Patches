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
#ifndef __CSTOR_DDP_H__
#define __CSTOR_DDP_H__

#include "cstor_umain.h"

#define PPOD_IDX_SHIFT		6
#define PPOD_SIZE_SHIFT		6
#define PPOD_PAGES_MAX		4

#define PPOD_VALID_SHIFT        24
#define PPOD_VALID(x)           ((x) << PPOD_VALID_SHIFT)
#define PPOD_VALID_FLAG         PPOD_VALID(1U)

#define PPOD_TID_SHIFT          0
#define PPOD_TID(x)             ((x) << PPOD_TID_SHIFT)

#define ULPMEM_IDATA_MAX_PPODS          (CSTOR_MAX_IMM_PPOD_DATA_LEN >> PPOD_SIZE_SHIFT)

#define CSTOR_ISCSI_NON_DDP_BIT	31

struct cstor_pagepod_hdr {
	u32 vld_tid;
	u32 pgsz_tag_clr;
	u32 max_offset;
	u32 page_offset;
	u64 rsvd;
};

struct cstor_pagepod {
	struct cstor_pagepod_hdr hdr;
	__be64 addr[PPOD_PAGES_MAX + 1];
};

struct cstor_tag_format {
	u8 free_bits:4;
	u8 color_bits:4;
	u8 idx_bits;
	u8 rsvd_bits;
	u32 idx_mask;
	u32 color_mask;
	u32 idx_clr_mask;
	u32 rsvd_mask;
};

struct cstor_bitmap {
	u16 num_long;
	u32 bits_in_zone;
	u64 base[];
};

struct cstor_ppm_zone {
	u32 ppod_nums;
	u32 ppod_start;
	u32 ppod_per_bit;
	struct cstor_bitmap *bm;
};

struct cstor_ppm_pool {
	u32 num_zones;
	struct cstor_ppm_zone *zones;
};

struct cstor_ppod_data {
	bool is_edram_idx;
	u8 color:6;
	u16 num_ppods;
	u32 zone_idx;
	u32 pool_idx;
};

struct cstor_ppm {
	struct cstor_tag_format tformat;
	struct cstor_ppm_pool *ddr_pool;
	struct cstor_ppm_pool *edram_pool;
	struct cstor_udevice *ucdev;
	u32 num_cores;
	u32 edram_ppmax;
	u32 ddr_ppmax;
	u32 max_ppods;
	u32 ppod_llimit;
	u32 base_idx;
	u16 refcnt;
	struct cstor_ppod_data ppod_data[];
};

struct cstor_uqp;

int
build_iscsi_ddp_wr(struct cstor_uqp *uqp, union t4_wr *wqe,
		   struct cstor_send_wr *wr, void *dsgl_buf, u16 pidx, u8 len16);
int
build_iscsi_invalidate_tag_wr(struct cstor_uqp *uqp, union t4_wr *wqe,
			      struct cstor_send_wr *wr, void *dsgl, u16 pidx, u8 len16);
#endif
