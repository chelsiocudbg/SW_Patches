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

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "cstor_umain.h"
#include "cstor_uiscsi_ddp.h"

u32 cstor_fls(u32 val)
{
	u32 i = sizeof(u32) * 8;

	while (i) {
		if (val & (1U << (i - 1)))
			return i;
		i--;
	}

	return 0;
}

static void cstor_put_ddp_idx(struct cstor_ppm *ppm, struct cstor_ppod_data *pdata, u32 ppod_idx)
{
	struct cstor_ppm_pool *pool;
	struct cstor_ppm_zone *zone;
	u32 pool_idx = pdata->pool_idx;
	u32 zone_idx = pdata->zone_idx;
	u32 i, idx;
	u32 bits_in_long = sizeof(unsigned long) * 8;

	if (pdata->is_edram_idx)
		pool = &ppm->edram_pool[pool_idx];
	else
		pool = &ppm->ddr_pool[pool_idx];

	zone = &pool->zones[zone_idx];

	idx = (ppod_idx - zone->ppod_start) / zone->ppod_per_bit;
	i = idx / bits_in_long;
	idx %= bits_in_long;

	zone->bm->base[i] |= (1UL << idx);
}

static int cstor_ppm_ppod_release(struct cstor_ppm *ppm, u32 ppod_idx)
{
	struct cstor_ppod_data *pdata;

	if (unlikely(ppod_idx >= ppm->max_ppods)) {
		cstor_err(ppm->ucdev, CSTOR_NOLOG, "ippm: idx too big %u "
			  "ddr_ppmax: %u edram_ppmax: %u.\n",
			  ppod_idx, ppm->ddr_ppmax, ppm->edram_ppmax);
		return EINVAL;
	}

	pdata = ppm->ppod_data + ppod_idx;
	if (unlikely(!pdata->num_ppods)) {
		cstor_err(ppm->ucdev, CSTOR_NOLOG, "ippm: idx %u, num_ppods 0.\n", ppod_idx);
		return EINVAL;
	}

	cstor_debug(ppm->ucdev, CSTOR_LOG, "release idx %u, num_ppods %u.\n",
		    ppod_idx, pdata->num_ppods);
	cstor_put_ddp_idx(ppm, pdata, ppod_idx);

	return 0;
}

static int
cstor_ppod_write_dsgl_ppod(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
			   void *dsgl, u32 ppod_idx, u32 num_ppods, u16 last_pidx, u8 len16)
{
	struct fw_nvmet_v2_fr_nsmr_wr *nsmr_wr = &wqe->nsmr_wr;

	nsmr_wr->op_to_wrid = htobe32(V_FW_WR_OP(FW_NVMET_V2_FR_NSMR_WR) |
				      V_FW_WR_COMPL(is_completion_needed(uqp, wr->flags)) |
				      V_FW_NVMET_V2_FR_NSMR_WR_WRID(last_pidx));
	nsmr_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(uqp->ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	nsmr_wr->r3 = 0;
	nsmr_wr->r4 = 0;
	nsmr_wr->mem_write_addr32 = htobe32((uqp->ucdev->iscsi_ppm->ppod_llimit +
					    (ppod_idx << PPOD_SIZE_SHIFT)) >> 5);
	nsmr_wr->r5 = 0;
	nsmr_wr->imm_data_len32 = 0;
	nsmr_wr->dsgl_data_len32 = htobe16((num_ppods << PPOD_SIZE_SHIFT) >> 5);
	nsmr_wr->r6 = 0;

	memcpy(nsmr_wr + 1, dsgl, (len16 << 4) - sizeof(*nsmr_wr));

	return 0;
}

static int
cstor_ppod_write_imm_ppod(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
			  u32 ppod_idx, u32 num_ppods, u16 last_pidx, u8 len16)
{
	struct fw_nvmet_v2_fr_nsmr_wr *nsmr_wr = &wqe->nsmr_wr;
	u32 dlen = num_ppods << PPOD_SIZE_SHIFT;

	nsmr_wr->op_to_wrid = htobe32(V_FW_WR_OP(FW_NVMET_V2_FR_NSMR_WR) |
				      V_FW_WR_COMPL(is_completion_needed(uqp, wr->flags)) |
				      V_FW_NVMET_V2_FR_NSMR_WR_WRID(last_pidx));
	nsmr_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(uqp->ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	nsmr_wr->r3 = 0;
	nsmr_wr->r4 = 0;
	nsmr_wr->mem_write_addr32 = htobe32((uqp->ucdev->iscsi_ppm->ppod_llimit +
					    (ppod_idx << PPOD_SIZE_SHIFT)) >> 5);
	nsmr_wr->r5 = 0;
	nsmr_wr->imm_data_len32 = dlen >> 5;
	nsmr_wr->dsgl_data_len32 = 0;
	nsmr_wr->r6 = 0;

	memcpy(nsmr_wr + 1, (void *)wr->sg_list->addr, dlen);

	return 0;
}

int
build_iscsi_invalidate_tag_wr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
			      void *dsgl, u16 last_pidx, u8 len16)
{
	struct fw_nvmet_v2_fr_nsmr_wr *nsmr_wr = &wqe->nsmr_wr;
	struct cstor_ppm *ppm = uqp->ucdev->iscsi_ppm;
	u32 ppod_idx = (wr->iscsi.ddp_tag >> PPOD_IDX_SHIFT) - ppm->base_idx;
	u32 dlen = ((ppm->ppod_data + ppod_idx)->num_ppods) << PPOD_SIZE_SHIFT;

	nsmr_wr->op_to_wrid = htobe32(V_FW_WR_OP(FW_NVMET_V2_FR_NSMR_WR) |
				      V_FW_WR_COMPL(is_completion_needed(uqp, wr->flags)) |
				      F_FW_NVMET_V2_FR_NSMR_WR_RESET_MEM |
				      V_FW_NVMET_V2_FR_NSMR_WR_WRID(last_pidx));
	nsmr_wr->flowid_len16 = htobe32(V_FW_WR_FLOWID(uqp->ucsk->csk.tid) | V_FW_WR_LEN16(len16));
	nsmr_wr->r3 = 0;
	nsmr_wr->r4 = 0;
	nsmr_wr->mem_write_addr32 = htobe32((uqp->ucdev->iscsi_ppm->ppod_llimit +
					    (ppod_idx << PPOD_SIZE_SHIFT)) >> 5);
	nsmr_wr->r5 = 0;
	nsmr_wr->imm_data_len32 = 0;
	nsmr_wr->reset_mem_len32 = htobe16(dlen >> 5);
	nsmr_wr->r6 = 0;

	return 0;
}

int
build_iscsi_ddp_wr(struct cstor_uqp *uqp, union t4_wr *wqe, struct cstor_send_wr *wr,
		   void *dsgl, u16 last_pidx, u8 len16)
{
	struct cstor_ppm *ppm = uqp->ucdev->iscsi_ppm;
	u32 ppod_idx = (wr->iscsi.ddp_tag >> PPOD_IDX_SHIFT) - ppm->base_idx;
	u32 num_ppods = (ppm->ppod_data + ppod_idx)->num_ppods;

	if (num_ppods <= ULPMEM_IDATA_MAX_PPODS)
		return cstor_ppod_write_imm_ppod(uqp, wqe, wr, ppod_idx, num_ppods,
						 last_pidx, len16);
	else
		return cstor_ppod_write_dsgl_ppod(uqp, wqe, wr, dsgl, ppod_idx, num_ppods,
						  last_pidx, len16);
}

void cstor_get_iscsi_non_ddp_tag(u32 *tag)
{
	*tag |= (1U << CSTOR_ISCSI_NON_DDP_BIT);
}

static void
cstor_ppm_make_ppod_hdr(struct cstor_ppm *ppm, u32 tag, u32 tid, u32 length,
			struct cstor_pagepod_hdr *hdr, u32 offset)
{
	hdr->vld_tid = htobe32(PPOD_VALID_FLAG | PPOD_TID(tid));
	hdr->pgsz_tag_clr = htobe32(tag & ppm->tformat.idx_clr_mask);
	hdr->max_offset = htobe32(length);
	hdr->page_offset = htobe32(offset);
	hdr->rsvd = 0;
}

static int
cstor_make_ppod(struct cstor_iscsi_ddp_tag_info *tinfo, u32 first_page_offset,
		u32 ddp_tag, u32 num_ppods)
{
	struct cstor_uqp *uqp = to_cstor_uqp(tinfo->qp);
	struct cstor_pagepod_hdr ppod_hdr = {};
	struct cstor_pagepod *ppod = (struct cstor_pagepod *)tinfo->ppod_sge->addr;
	struct cstor_sge *sge, sg;
	u64 addr;
	u32 length;
	u32 ddp_page_size = uqp->iscsi_ddp_page_size;
	u32 i, j, len, idx;
	u32 num_ppod_dsgl = 0;
	bool non_contiguous;
	int ret;

	if (num_ppods > ULPMEM_IDATA_MAX_PPODS) {
		sg = *tinfo->ppod_sge;
		while (sg.length) {
			ret = cstor_virt_to_dma_addr(uqp->ucdev, &sg, &addr, &length,
						     &non_contiguous);
			if (unlikely(ret)) {
				cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_virt_to_dma_addr() "
					  "failed, ret %d\n", ret);
				return ret;
			}

			sg.addr += length;
			sg.length -= length;
			num_ppod_dsgl++;
		}

		if (unlikely(num_ppod_dsgl > 3)) {
			cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid num_ppod_dsgl %u\n",
				  num_ppod_dsgl);
			return EINVAL;
		}
	}

	cstor_ppm_make_ppod_hdr(uqp->ucdev->iscsi_ppm, ddp_tag, uqp->ucsk->csk.tid,
				tinfo->transfer_len, &ppod_hdr, first_page_offset);
	j = 0;
	idx = 0;
	sge = tinfo->sg_list;
	for (i = 0; i < tinfo->num_sge; i++) {
		sg = sge[i];

		len = sg.length;
		if (!i) {
			sg.addr -= first_page_offset;
			len += first_page_offset;
		}

		while (len) {
			if (len >= ddp_page_size) {
				sg.length = ddp_page_size;
			} else {
				if (unlikely(i != (tinfo->num_sge - 1))) {
					cstor_err(uqp->ucdev, CSTOR_NOLOG,
						  "invalid sge, cannot program ddp, "
						  "first_page_offset %u ddp_page_size %u "
						  "len %u transfer_len %u num_sge %u i %u\n",
						  first_page_offset, ddp_page_size, len,
						  tinfo->transfer_len, tinfo->num_sge, i);
					return EINVAL;
				}
				sg.length = len;
			}

			ret = cstor_virt_to_dma_addr(uqp->ucdev, &sg, &addr, &length,
						     &non_contiguous);
			if (unlikely(ret || (addr % ddp_page_size) || non_contiguous)) {
				cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_virt_to_dma_addr() "
					  "failed or addr not align with ddp_page_size, "
					  "ret %d addr %llu non_contiguous %u\n", ret, addr, non_contiguous);
				return non_contiguous ? EINVAL : ret;
			}

			sg.addr += sg.length;
			len -= sg.length;

			ppod[j].addr[idx++] = htobe64(addr);
			if (idx == (PPOD_PAGES_MAX + 1)) {
				memcpy(ppod + j, &ppod_hdr, sizeof(struct cstor_pagepod_hdr));
				j++;
				idx = 0;
				if (j < num_ppods)
					ppod[j].addr[idx++] = htobe64(addr);
			}
		}
	}

	if (idx) {
		memcpy(ppod + j, &ppod_hdr, sizeof(struct cstor_pagepod_hdr));
		for (i = idx; i < (PPOD_PAGES_MAX + 1); i++)
			ppod[j].addr[i] = 0;
	}

	return 0;
}

static void
cstor_build_ppod_data(struct cstor_ppod_data *pdata, u32 pool_idx, u32 zone_idx, u32 num_ppods,
		      bool is_edram_idx)
{
	pdata->num_ppods = num_ppods;
	pdata->pool_idx = pool_idx;
	pdata->zone_idx = zone_idx;
	pdata->is_edram_idx = is_edram_idx;

	if (pdata->color == ((1 << PPOD_IDX_SHIFT) - 1))
		pdata->color = 1;
	else
		pdata->color++;
}

static int cstor_get_ddp_idx(struct cstor_ppm_zone *zone)
{
	struct cstor_bitmap *bm = zone->bm;
	int idx = -1;
	u32 i;
	u32 bits_in_long = sizeof(unsigned long) * 8;

	for (i = 0; i < bm->num_long; i++) {
		idx = ffsl(bm->base[i]);
		if (idx > 0) {
			if (((i * bits_in_long) + idx) > bm->bits_in_zone)
				idx = -1;
			else
				bm->base[i] &= ~(1UL << (idx - 1));

			break;
		}
	}

	return idx <= 0 ? -1 : ((i * bits_in_long) + (idx - 1));
}

static int cstor_find_best_fit(u32 *ppod_per_bit, u32 num_ppods, u32 start)
{
	int i;

	for (i = start; i >= 0; i--) {
		if (num_ppods <= ppod_per_bit[i])
			return i;
	}

	return -1;
}

static int
cstor_get_ddp_tag(struct cstor_ppm *ppm, struct cstor_iscsi_ddp_tag_info *tinfo, u32 *ddp_tag,
		  u32 num_ppods, bool is_edram)
{
	struct cstor_ppm_pool *pool;
	struct cstor_ppm_zone *zone;
	struct cstor_ppod_data *pdata;
	int idx, zone_idx;
	u32 *ppod_per_bit;
	u32 hwidx;
	u32 start;

	if (is_edram) {
		pool = &ppm->edram_pool[tinfo->pool_idx];
		ppod_per_bit = ppm->ucdev->edram_ppod_per_bit;
		start = ppm->ucdev->num_edram_zones - 1;
	} else {
		pool = &ppm->ddr_pool[tinfo->pool_idx];
		ppod_per_bit = ppm->ucdev->ddr_ppod_per_bit;
		start = ppm->ucdev->num_ddr_zones - 1;
	}

	do {
		zone_idx = cstor_find_best_fit(ppod_per_bit, num_ppods, start);
		if (zone_idx < 0) {
			cstor_debug(ppm->ucdev, CSTOR_NOLOG, "cstor_find_best_fit() failed, "
				    "start %u num_ppods %u zone_idx %d\n",
				    start, num_ppods, zone_idx);
			return ENOTSUP;
		}

		zone = &pool->zones[zone_idx];
		idx = cstor_get_ddp_idx(zone);
		start = zone_idx - 1;
	} while (idx < 0 && zone_idx);

	if (idx < 0)
		return ENOMEM;

	/* Actual ppod index */
	idx = zone->ppod_start + (idx  * zone->ppod_per_bit);
	pdata = ppm->ppod_data + idx;
	hwidx = ppm->base_idx + idx;

	cstor_build_ppod_data(pdata, tinfo->pool_idx, zone_idx, num_ppods, is_edram);

	*ddp_tag = ((hwidx << PPOD_IDX_SHIFT) | ((u32)(pdata->color)));

	return 0;
}

int cstor_alloc_iscsi_ddp_tag(struct cstor_iscsi_ddp_tag_info *tinfo, u32 *ddp_tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(tinfo->qp);
	struct cstor_ppm *ppm = uqp->ucdev->iscsi_ppm;
	u32 first_page_offset = tinfo->sg_list->addr % uqp->iscsi_ddp_page_size;
	u32 tag;
	u32 num_ppods;
	int ret = ENOMEM;

	if (unlikely(!tinfo->transfer_len)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "invalid, transfer len is 0\n");
		return EINVAL;
	}

	num_ppods = DIV_ROUND_UP(tinfo->transfer_len + first_page_offset,
				 uqp->iscsi_ddp_page_size * PPOD_PAGES_MAX);

	if (ppm->edram_pool)
		ret = cstor_get_ddp_tag(ppm, tinfo, &tag, num_ppods, true);

	if (ret) {
		ret = cstor_get_ddp_tag(ppm, tinfo, &tag, num_ppods, false);
		if (ret)
			return ret;
	}

	ret = cstor_make_ppod(tinfo, first_page_offset, tag, num_ppods);
	if (unlikely(ret)) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_make_ppod() failed, "
			  "first_page_offset %u num_ppods %u ret %d\n",
			  first_page_offset, num_ppods, ret);
		cstor_ppm_ppod_release(ppm, (tag >> PPOD_IDX_SHIFT) - ppm->base_idx);
		return ret;
	}

	*ddp_tag = tag;

	return 0;
}

int cstor_free_iscsi_ddp_tag(struct cstor_qp *qp, u32 ddp_tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_ppm *ppm = uqp->ucdev->iscsi_ppm;
	u32 ppod_idx;
	int ret;

	if (unlikely(ddp_tag & (1U << CSTOR_ISCSI_NON_DDP_BIT))) {
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_free_iscsi_ddp_tag() called "
			  "on a non ddp tag %u\n", ddp_tag);
		return EINVAL;
	}

	ppod_idx = (ddp_tag >> PPOD_IDX_SHIFT) - ppm->base_idx;

	ret = cstor_ppm_ppod_release(ppm, ppod_idx);
	if (unlikely(ret))
		cstor_err(uqp->ucdev, CSTOR_NOLOG, "cstor_ppm_ppod_release() failed, "
			  "ret %d\n", ret);

	return ret;
}

static void cstor_ppm_free_cpu_pool(struct cstor_ppm_pool *pool, u32 num_cores, u32 num_zones)
{
	u32 i, core;

	if (!pool) {
		cstor_printf(stderr, CSTOR_NOLOG, "invalid ppm pool is NULL\n");
		return;
	}

	for (core = 0; core < num_cores; core++) {
		for (i = 0; i < num_zones; i++) {
			if (pool[core].zones)
				free(pool[core].zones[i].bm);
		}

		free(pool[core].zones);
	}

	free(pool);
}

static void cstor_ppm_free(struct cstor_ppm *ppm)
{
	if (ppm->edram_pool)
		cstor_ppm_free_cpu_pool(ppm->edram_pool, ppm->num_cores,
					ppm->ucdev->num_edram_zones);

	cstor_ppm_free_cpu_pool(ppm->ddr_pool, ppm->num_cores, ppm->ucdev->num_ddr_zones);

	ppm->ucdev->iscsi_ppm = NULL;

	free(ppm);
}

void cstor_release_iscsi_ddp(struct cstor_device *cdev)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);

	if (!ucdev->iscsi_ppm)
		return;

	pthread_spin_lock(&ucdev->lock);
	if (--ucdev->iscsi_ppm->refcnt) {
		pthread_spin_unlock(&ucdev->lock);
		return;
	}

	cstor_set_iscsi_region_status(ucdev, CSTOR_ISCSI_REGION_FREE);
	cstor_ppm_free(ucdev->iscsi_ppm);
	pthread_spin_unlock(&ucdev->lock);
}

static void cstor_ppm_ppod_data_init(struct cstor_ppod_data *pdata, u32 num_ppods)
{
	u32 i;

	for (i = 0; i < num_ppods; i++)
		pdata[i].color = 1;
}

static void cstor_bitmap_init(struct cstor_bitmap *bm, u32 bits_in_zone, u32 size)
{
	bm->num_long = (size - sizeof(struct cstor_bitmap)) >> 3;
	bm->bits_in_zone = bits_in_zone;
	memset(bm->base, 0xff, bm->num_long << 3);
}

static u32 cstor_bitmap_size(u32 bits_in_zone)
{
	return (sizeof(struct cstor_bitmap) + ((align(bits_in_zone, 64)) >> 3));
}

static void
cstor_ppm_get_num_ppod(u32 *zone_num_ppod, u32 *zone_percentage, u32 ppmax, u32 num_zones)
{
	u32 count = 0, i;

	for (i = 0; i < num_zones; i++) {
		zone_num_ppod[i] = zone_percentage[i] * ppmax / 100;
		count += zone_num_ppod[i];
	}

	/*
	 * Put the remaining ppods in the lowest ppod per entry zone
	 * Here the assumption is last zone has the lowest ppod per entry
	 */

	zone_num_ppod[num_zones - 1] += (ppmax - count);
}

static int
cstor_ppm_alloc_cpu_pool(struct cstor_udevice *ucdev, struct cstor_ppm_pool **pool,
			 u32 *total_ppods, u32 num_cores, u32 ppod_start, bool is_edram)
{
	struct cstor_ppm_pool *p;
	struct cstor_ppm_zone *zone;
	u32 *ppod_per_bit;
	u32 *zone_percentage;
	u32 ppmax = (*total_ppods) / num_cores;
	u32 num_of_ppod, count = ppod_start;
	u32 num_zones = is_edram ? ucdev->num_edram_zones : ucdev->num_ddr_zones;
	u32 zone_num_ppod[num_zones];
	u32 bits_in_zone, bm_size;
	u32 core, i;
	int ret;

	if (!ppmax) {
		cstor_err(ucdev, CSTOR_NOLOG, "ppod per core is 0, total_ppods %u num_cores %u\n",
			  *total_ppods, num_cores);
		return EINVAL;
	}

	if (is_edram) {
		ppod_per_bit = ucdev->edram_ppod_per_bit;
		zone_percentage = ucdev->edram_ppod_zone_percentage;
	} else {
		ppod_per_bit = ucdev->ddr_ppod_per_bit;
		zone_percentage = ucdev->ddr_ppod_zone_percentage;
	}

	p = calloc(num_cores, sizeof(struct cstor_ppm_pool));
	if (!p) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate pool, num_cores %u\n", num_cores);
		return ENOMEM;
	}

	cstor_ppm_get_num_ppod(zone_num_ppod, zone_percentage, ppmax, num_zones);

	for (core = 0; core < num_cores; core++) {
		p[core].zones = calloc(num_zones, sizeof(struct cstor_ppm_zone));
		if (!p[core].zones) {
			cstor_err(ucdev, CSTOR_NOLOG, "p[%u].zones alloc failed, num_zones %u\n",
				  core, num_zones);
			ret = ENOMEM;
			goto err;
		}

		for (i = 0; i < num_zones; i++) {
			zone = &p[core].zones[i];

			num_of_ppod = zone_num_ppod[i];
			bits_in_zone = (num_of_ppod / ppod_per_bit[i]);
			bm_size = cstor_bitmap_size(bits_in_zone);
			zone->bm = malloc(bm_size);
			if (!zone->bm) {
				cstor_err(ucdev, CSTOR_NOLOG, "failed to allocate zone->bm, "
					  "bm_size %u\n", bm_size);
				ret = ENOMEM;
				goto err;
			}

			cstor_bitmap_init(zone->bm, bits_in_zone, bm_size);

			zone->ppod_nums = num_of_ppod;
			zone->ppod_start = count;
			zone->ppod_per_bit = ppod_per_bit[i];
			count += num_of_ppod;
		}
	}

	*total_ppods = count;
	*pool = p;

	return 0;
err:
	cstor_ppm_free_cpu_pool(p, num_cores, num_zones);
	return ret;
}

static void cstor_build_tformat(struct cstor_udevice *ucdev, struct cstor_tag_format *tformat)
{
	u32 bits = cstor_fls(ucdev->iscsi_tagmask);

	/* reserve top most 2 bits for page selector */
	tformat->free_bits = 32 - 2 - bits;
	tformat->rsvd_bits = bits;
	tformat->color_bits = PPOD_IDX_SHIFT;
	tformat->idx_bits = bits - 1 - PPOD_IDX_SHIFT;
	tformat->idx_mask = (1U << tformat->idx_bits) - 1;
	tformat->color_mask = (1U << PPOD_IDX_SHIFT) - 1;
	tformat->idx_clr_mask = (1U << (bits - 1)) - 1;
	tformat->rsvd_mask = (1U << bits) - 1;

	cstor_debug(ucdev, CSTOR_NOLOG, "ippm: tagmask %#x, rsvd_bits %u=%u+%u+1, rsvd_mask %#x\n",
		    ucdev->iscsi_tagmask, tformat->rsvd_bits, tformat->idx_bits,
		    tformat->color_bits, tformat->rsvd_mask);
}

static int cstor_ppm_init(struct cstor_udevice *ucdev, u32 num_cores)
{
	struct cstor_ppm *ppm;
	u32 ppod_region_start = ucdev->ppod_start;
	u32 iscsi_edram_size = ucdev->edram_start ? ucdev->edram_size : 0;
	u32 edram_ppmax, ddr_ppmax, max_ppods;
	u32 alloc_sz;
	int ret = 0;

	if (iscsi_edram_size && ((ucdev->edram_start + iscsi_edram_size) != ppod_region_start)) {
		cstor_err(ucdev, CSTOR_NOLOG, "iscsi ppod region not contiguous: "
			  "EDRAM start %#x size %#x DDR start %#x\n",
			  ucdev->edram_start, iscsi_edram_size, ppod_region_start);
		return EINVAL;
	}

	edram_ppmax = iscsi_edram_size >> PPOD_SIZE_SHIFT;
	ddr_ppmax = ucdev->iscsi_region_size >> PPOD_SIZE_SHIFT;
	alloc_sz = sizeof(struct cstor_ppm) +
		   (ddr_ppmax + edram_ppmax) * (sizeof(struct cstor_ppod_data));

	ppm = calloc(1, alloc_sz);
	if (!ppm) {
		cstor_err(ucdev, CSTOR_NOLOG, "ppm alloc failed (alloc_sz: %u)\n", alloc_sz);
		return ENOMEM;
	}

	if (iscsi_edram_size) {
		max_ppods = edram_ppmax;
		ppod_region_start = ucdev->edram_start;
		ret = cstor_ppm_alloc_cpu_pool(ucdev, &ppm->edram_pool, &max_ppods,
					       num_cores, 0, true);
		if (ret) {
			cstor_err(ucdev, CSTOR_NOLOG, "cstor_ppm_alloc_cpu_pool() failed, "
				  "edram_ppmax %u num_cores %u ret %d\n",
				  edram_ppmax, num_cores, ret);
			goto free_ppm;
		}
	}

	max_ppods = ddr_ppmax;
	ret = cstor_ppm_alloc_cpu_pool(ucdev, &ppm->ddr_pool, &max_ppods,
				       num_cores, edram_ppmax, false);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "cstor_ppm_alloc_cpu_pool() failed, "
			  "ddr_ppmax %u num_cores %u ppod_start %u ret %d\n",
			   ddr_ppmax, num_cores, edram_ppmax, ret);
		goto free_edram_pool;
	}

	cstor_build_tformat(ucdev, &ppm->tformat);

	ppm->refcnt = 1;
	ppm->num_cores = num_cores;
	ppm->ucdev = ucdev;
	ppm->ddr_ppmax = ddr_ppmax;
	ppm->edram_ppmax = edram_ppmax;
	ppm->max_ppods = max_ppods;
	ppm->ppod_llimit = ucdev->ppod_llimit;
	ppm->base_idx = ppod_region_start > ucdev->ppod_llimit ?
			(ppod_region_start - ucdev->ppod_llimit + 1) >> PPOD_SIZE_SHIFT : 0;

	cstor_ppm_ppod_data_init(ppm->ppod_data, max_ppods);

	ucdev->iscsi_ppm = ppm;

	return 0;

free_edram_pool:
	if (ppm->edram_pool)
		cstor_ppm_free_cpu_pool(ppm->edram_pool, num_cores, ucdev->num_edram_zones);
free_ppm:
	free(ppm);
	return ret;
}

int cstor_invalidate_iscsi_ddp_tag(struct cstor_qp *qp, u32 *tags, u32 num_tag)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	struct cstor_udevice *ucdev = uqp->ucdev;
	struct cstor_ppm *ppm = ucdev->iscsi_ppm;
	struct cstor_invalidate_iscsi_tag_cmd cmd = {};
	struct cstor_ppod_data *pdata;
	struct cstor_iscsi_tag_info *tinfo = cmd.tinfo;
	u32 i, ppod_idx;
	int ret;

	if (num_tag > CSTOR_MAX_INVALIDATE_ISCSI_TAG) {
		cstor_err(ucdev, CSTOR_NOLOG, "invalid num_tag %u > "
			  "CSTOR_MAX_INVALIDATE_ISCSI_TAG %u\n",
			  num_tag, CSTOR_MAX_INVALIDATE_ISCSI_TAG);
		return EINVAL;
	}

	for (i = 0; i < num_tag; i++) {
		ppod_idx = (tags[i] >> PPOD_IDX_SHIFT) - ppm->base_idx;
		pdata = ppm->ppod_data + ppod_idx;
		if (!pdata->num_ppods) {
			cstor_err(ucdev, CSTOR_NOLOG, "ippm: idx %u, num_ppods 0.\n", ppod_idx);
			return EINVAL;
		}

		tinfo->pm_addr = (ppod_idx << PPOD_SIZE_SHIFT) + ppm->ppod_llimit;
		tinfo->dlen = pdata->num_ppods << PPOD_SIZE_SHIFT;
		tinfo++;
	}

	cmd.count = num_tag;

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_INVALIDATE_ISCSI_TAG, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_INVALIDATE_ISCSI_TAG cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

int cstor_init_iscsi_ddp(struct cstor_device *cdev, u32 num_cores)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	int ret;

	if (!ucdev->iscsi_region_size) {
		cstor_err(ucdev, CSTOR_NOLOG, "iscsi_region unavailable\n");
		return EACCES;
	}

	pthread_spin_lock(&ucdev->lock);
	if (ucdev->iscsi_ppm) {
		ucdev->iscsi_ppm->refcnt++;
		ret = 0;
		goto out;
	}

	ret = cstor_ppm_init(ucdev, num_cores);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "cstor_ppm_init() failed, ret %d\n", ret);
		goto out;
	}

	ret = cstor_set_iscsi_region_status(ucdev, CSTOR_ISCSI_REGION_INUSE);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "cstor_set_iscsi_region_status() "
			  "failed, ret %d\n", ret);
		cstor_ppm_free(ucdev->iscsi_ppm);
		goto out;
	}

out:
	pthread_spin_unlock(&ucdev->lock);
	return ret;
}

u32 cstor_get_iscsi_ppod_buf_len(struct cstor_qp *qp, u64 first_page_addr, u32 transfer_len)
{
	struct cstor_uqp *uqp = to_cstor_uqp(qp);
	u32 ddp_page_size = uqp->iscsi_ddp_page_size;
	u32 num_ppods = DIV_ROUND_UP(transfer_len + (first_page_addr % ddp_page_size),
				     ddp_page_size * PPOD_PAGES_MAX);

	return num_ppods << PPOD_SIZE_SHIFT;
}
