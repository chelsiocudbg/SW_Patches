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
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <linux/pci.h>

#include "cstor_umain.h"

u64 cstor_page_size;
u64 cstor_page_mask;

LIST_HEAD(devices);

pthread_mutex_t cstor_dev_lock = PTHREAD_MUTEX_INITIALIZER;

u32
cstor_get_mdsl(struct cstor_device *cdev, u32 hlen, u32 pad_bytes, u8 hdgst_enabled,
	       u8 ddgst_enabled)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	u32 mdsl;

	mdsl = ucdev->max_pdu_size - hlen - pad_bytes;

	if (hdgst_enabled)
		mdsl -= 4;

	if (ddgst_enabled)
		mdsl -= 4;

	mdsl = ROUND_DOWN(mdsl, 4U);

	if (mdsl > 32768)
		mdsl = 32768;
	else if (mdsl > 16384)
		mdsl = 16384;

	return mdsl;
}

static int cstor_alloc_udevice(struct cstor_udevice *ucdev)
{
	struct cstor_device *cdev = &ucdev->cdev;
	struct cstor_query_device_cmd qcmd = {};
	struct _cstor_device_attr *_attr;
	u32 i;
	int ret;

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_QUERY_DEVICE, &qcmd);
	if (ret) {
		cstor_printf(stderr, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_QUERY_DEVICE cmd, "
			     "fd %d ret %d\n", cdev->dev_fd, ret);
		return ret;
	}

	_attr = &qcmd.resp.attr;

	ret = snprintf(cdev->name, sizeof(cdev->name), "%s", (char *)_attr->name);
	if (ret < 0) {
		cstor_printf(stderr, CSTOR_NOLOG,
			     "snprintf() failed to format dev name, ret %d\n", ret);
		return EINVAL;
	}

	cdev->num_ports = _attr->num_ports;
	ucdev->plat_dev = _attr->plat_dev;

	ucdev->stid_base = _attr->stid_base;

	ucdev->stid2ptr = calloc(_attr->max_listen_sock, sizeof(void *));
	if (!ucdev->stid2ptr) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucdev->stid2ptr allocation\n");
		goto err;
	}

	ucdev->tid_base = _attr->tid_base;

	ucdev->tid2ptr = calloc(_attr->max_sock, sizeof(void *));
	if (!ucdev->tid2ptr) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucdev->tid2ptr allocation\n");
		goto err;
	}

	ucdev->max_pdu_size = _attr->max_pdu_size;

	ucdev->atid2ptr = calloc(_attr->max_atids, sizeof(void *));
	if (!ucdev->atid2ptr) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucdev->atid2ptr allocation\n");
		goto err;
	}

	if (_attr->wc_enabled) {
		cstor_info(ucdev, CSTOR_NOLOG, "wc_enabled\n");
		ucdev->wc_enabled = true;
	}

	ucdev->mmid2ptr = calloc(_attr->max_mr, sizeof(void *));
	if (!ucdev->mmid2ptr) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucdev->mmid2ptr allocation\n");
		goto err;
	}

	/* NVMe/TCP attribute */
	ucdev->stag_start_addr32 = _attr->nvme.stag_start_addr32;

	/* iSCSI attributes */
	ucdev->iscsi_region_size = _attr->iscsi.region_size;
	ucdev->iscsi_tagmask = _attr->iscsi.iscsi_tagmask;
	ucdev->ppod_llimit = _attr->iscsi.ppod_llimit;
	ucdev->ppod_start = _attr->iscsi.ppod_start;
	ucdev->edram_start = _attr->iscsi.edram_start;
	ucdev->edram_size = _attr->iscsi.edram_size;
	if (ucdev->edram_size) {
		ucdev->num_edram_zones = _attr->iscsi.num_edram_zones;
		for (i = 0; i < ucdev->num_edram_zones; i++) {
			ucdev->edram_ppod_zone_percentage[i] =
				_attr->iscsi.edram_ppod_zone_percentage[i];
			ucdev->edram_ppod_per_bit[i] = _attr->iscsi.edram_ppod_per_bit[i];
		}
	}

	ucdev->num_ddr_zones = _attr->iscsi.num_ddr_zones;
	for (i = 0; i < ucdev->num_ddr_zones; i++) {
		ucdev->ddr_ppod_zone_percentage[i] = _attr->iscsi.ddr_ppod_zone_percentage[i];
		ucdev->ddr_ppod_per_bit[i] = _attr->iscsi.ddr_ppod_per_bit[i];
	}

	ucdev->iscsi_ppm = NULL;

	return 0;

err:
	free(ucdev->atid2ptr);
	free(ucdev->tid2ptr);
	free(ucdev->stid2ptr);
	free(ucdev->mmid2ptr);

	return ENOMEM;
}

int cstor_get_devices(struct cstor_device **__cdev, u32 num_cdev)
{
	struct cstor_udevice *ucdev;
	u32 i = 0;

	if (!num_cdev) {
		cstor_printf(stderr, CSTOR_NOLOG, "invalid num_cdev %u\n", num_cdev);
		return EINVAL;
	}

	pthread_mutex_lock(&cstor_dev_lock);
	list_for_each(&devices, ucdev, list) {
		__cdev[i++] = &ucdev->cdev;
		if (i == num_cdev) {
			pthread_mutex_unlock(&cstor_dev_lock);
			return 0;
		}
	}
	pthread_mutex_unlock(&cstor_dev_lock);

	cstor_printf(stderr, CSTOR_NOLOG, "invalid i(%u) != num_cdev(%u)\n", i, num_cdev);
	return ENODEV;
}

static void cstor_close_device(struct cstor_udevice *ucdev)
{
	cstor_debug(ucdev, CSTOR_NOLOG, "ucdev->ref_count %u\n", ucdev->ref_count);

	ucdev->ref_count--;
	if (ucdev->ref_count)
		return;

	list_del(&ucdev->list);
	close(ucdev->cdev.dev_fd);
	free(ucdev->atid2ptr);
	free(ucdev->tid2ptr);
	free(ucdev->stid2ptr);
	free(ucdev->mmid2ptr);
	free(ucdev);
}

void cstor_close_devices(void)
{
	struct cstor_udevice *ucdev, *tmp;

	pthread_mutex_lock(&cstor_dev_lock);
	list_for_each_safe(&devices, ucdev, tmp, list)
		cstor_close_device(ucdev);
	pthread_mutex_unlock(&cstor_dev_lock);
}

static int __cstor_get_devices(u32 *num_cdev)
{
	struct cstor_udevice *ucdev, *tmp;
	char devname[20] = {'\0'};
	u32 minor;
	int fd, ret;

	for (minor = 0; minor < CSTOR_MAX_ADAPTERS; minor++) {
		ret = snprintf(devname, sizeof(devname), "/dev/" CSTOR_DRIVER_NAME "%u", minor);
		if (ret < 0) {
			cstor_printf(stderr, CSTOR_NOLOG,
				     "snprintf() failed to format dev node name, ret %d\n", ret);
			ret = EINVAL;
			goto out;
		}

		fd = open(devname, O_RDWR);
		if (fd < 0)
			continue;

		ucdev = calloc(1, sizeof(struct cstor_udevice));
		if (!ucdev) {
			cstor_printf(stderr, CSTOR_NOLOG, "failed to allocate memory for ucdev\n");
			ret = ENOMEM;
			close(fd);
			goto out;
		}

		ucdev->cdev.dev_fd = fd;
		pthread_spin_init(&ucdev->lock, PTHREAD_PROCESS_PRIVATE);
		pthread_mutex_init(&ucdev->mlock, NULL);

		ret = cstor_alloc_udevice(ucdev);
		if (ret) {
			cstor_printf(stderr, CSTOR_NOLOG, "cstor_alloc_udevice() failed, "
				     "ret %d\n", ret);
			close(fd);
			free(ucdev);
			goto out;
		}

		ucdev->ref_count++;
		list_add_tail(&devices, &ucdev->list);
		(*num_cdev)++;
	}

	if (list_empty(&devices)) {
		cstor_printf(stderr, CSTOR_NOLOG, "No device found\n");
		return ENODEV;
	}

	return 0;

out:
	*num_cdev = 0;

	list_for_each_safe(&devices, ucdev, tmp, list)
		cstor_close_device(ucdev);

	return ret;
}

static int cstor_module_version_check(void)
{
	const char *ver_file_name = "/sys/module/" CSTOR_DRIVER_NAME "/version";
	char ver[256];
	ssize_t nbytes;
	int fd;
	int ret = 0;

	fd = open(ver_file_name, O_RDONLY);
	if (fd < 0) {
		ret = errno;
		cstor_printf(stderr, CSTOR_NOLOG, "failed to open file: %s: %d\n",
			     ver_file_name, ret);
		return ret;
	}

	nbytes = read(fd, ver, sizeof(ver));
	if (nbytes <= 0) {
		ret = errno;
		cstor_printf(stderr, CSTOR_NOLOG, "failed to read file: %s errno: %d\n",
			     ver_file_name, nbytes ? ret : 0);
		ret = EIO;
		goto err;
	}

	ver[nbytes - 1] = '\0';

	if (strcmp(ver, CSTOR_MODULE_VERSION)) {
		cstor_printf(stderr, CSTOR_NOLOG, "driver version mismatch: required %s: "
			     "found %s\n", CSTOR_MODULE_VERSION, ver);
		ret = EINVAL;
	}
err:
	if (close(fd)) {
		ret = errno;
		cstor_printf(stderr, CSTOR_NOLOG, "failed to close file: %s errno: %d\n",
			     ver_file_name, errno);
	}

	return ret;
}

int cstor_open_devices(u32 *num_cdev)
{
	struct cstor_udevice *ucdev;
	int ret;

	ret = cstor_module_version_check();
	if (ret) {
		cstor_printf(stderr, CSTOR_NOLOG,
			     "cstor_module_version_check() failed, ret %d\n", ret);
		return ret;
	}

	*num_cdev = 0;

	pthread_mutex_lock(&cstor_dev_lock);
	if (list_empty(&devices)) {
		ret = __cstor_get_devices(num_cdev);
		if (ret) {
			cstor_printf(stderr, CSTOR_NOLOG,
				     "__cstor_get_devices() failed, ret %d\n", ret);
			pthread_mutex_unlock(&cstor_dev_lock);
			return ret;
		}
	} else {
		list_for_each(&devices, ucdev, list) {
			ucdev->ref_count++;
			(*num_cdev)++;
		}
	}
	pthread_mutex_unlock(&cstor_dev_lock);

	return 0;
}

static int
cstor_fill_cstor_device_attr(struct cstor_device_attr *attr, struct _cstor_device_attr *_attr)
{
	u64 raw_fw_ver = _attr->fw_ver;
	u32 i;
	u8 major = (raw_fw_ver >> 24) & 0xff;
	u8 minor = (raw_fw_ver >> 16) & 0xff;
	u8 sub_minor = (raw_fw_ver >> 8) & 0xff;
	u8 build = raw_fw_ver & 0xff;
	int ret;

	ret = snprintf(attr->name, sizeof(attr->name), "%s", (char *)_attr->name);
	if (ret < 0) {
		cstor_printf(stderr, CSTOR_NOLOG,
			     "snprintf() failed to format dev name, ret %d\n", ret);
		return EINVAL;
	}
	ret = snprintf(attr->fw_ver, sizeof(attr->fw_ver), "%d.%d.%d.%d",
		       major, minor, sub_minor, build);
	if (ret < 0) {
		cstor_printf(stderr, CSTOR_NOLOG,
			     "snprintf() failed to format fw_ver, ret %d\n", ret);
		return EINVAL;
	}

	for (i = 0; i < _attr->num_ports; i++) {
		ret = snprintf(attr->iface_name[i], sizeof(attr->iface_name[i]), "%s",
			       _attr->iface_name[i]);
		if (ret < 0) {
			cstor_printf(stderr, CSTOR_NOLOG,
				     "snprintf() failed to format iface_name, ret %d\n", ret);
			return EINVAL;
		}

		memcpy(&attr->mac_addr[i], &_attr->mac_addr[i], 6);
	}

	attr->vendor_id = _attr->vendor_id;
	attr->vendor_part_id = _attr->vendor_part_id;
	attr->numa_node_id = _attr->numa_node_id;
	attr->hw_ver = _attr->hw_ver;
	attr->max_qp = _attr->max_qp;
	attr->max_qp_wr = _attr->max_qp_wr;
	attr->max_send_sge = T4_MAX_SEND_SGE;
	attr->max_pd = _attr->max_pd;
	attr->max_lso_buf_size = _attr->max_lso_buf_size;
	attr->max_listen_sock = _attr->max_listen_sock;
	attr->max_sock = _attr->max_sock;
	attr->num_ports = _attr->num_ports;
	attr->wc_enabled = _attr->wc_enabled;
	attr->max_mr = _attr->max_mr;
	attr->max_mr_size = _attr->max_mr_size;
	attr->max_send_imm_data = T4_MAX_SEND_IMM_DATA;

	/* NVMe/TCP attributes */
	attr->page_size_cap = _attr->nvme.page_size_cap;
	attr->max_rq_sge = T4_MAX_RQ_SGE;
	attr->max_ddp_sge = _attr->nvme.max_ddp_sge;
	attr->max_ddp_tag = _attr->nvme.max_ddp_tag;
	attr->max_cq = _attr->nvme.max_cq;
	attr->max_cqe = _attr->nvme.max_cqe;
	attr->max_srq = _attr->nvme.max_srq;
	attr->max_srq_wr = _attr->nvme.max_srq_wr;
	attr->max_srq_sge = T4_MAX_RQ_SGE;

	/* iSCSI attributes */
	attr->fl_page_size_cap = _attr->iscsi.fl_page_size_cap;
	attr->iscsi_page_size_cap = _attr->iscsi.ddp_page_size_cap;
	attr->max_rxq_sge = T4_MAX_RXQ_SGE;

	return 0;
}

int cstor_query_device(struct cstor_device *cdev, struct cstor_device_attr *attr)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_query_device_cmd cmd = {};
	int ret;

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_QUERY_DEVICE, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_QUERY_DEVICE cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	if (cstor_fill_cstor_device_attr(attr, &cmd.resp.attr)) {
		cstor_err(ucdev, CSTOR_NOLOG, "cstor_fill_cstor_device_attr() failed");
		return EINVAL;
	}

	return 0;
}

struct cstor_pd *cstor_alloc_pd(struct cstor_device *cdev)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_upd *upd;
	struct cstor_alloc_pd_cmd cmd = {};
	int ret;

	upd = calloc(1, sizeof(*upd));
	if (!upd) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed upd allocation\n");
		errno = ENOMEM;
		return NULL;
	}

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_ALLOC_PD, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_ALLOC_PD cmd, "
			  "ret %d\n", ret);
		free(upd);
		errno = ret;
		return NULL;
	}

	upd->pd.cdev = cdev;
	upd->pd.pdid = cmd.resp.pdid;

	return &upd->pd;
}

int cstor_dealloc_pd(struct cstor_pd *pd)
{
	struct cstor_upd *upd = to_cstor_upd(pd);
	struct cstor_udevice *ucdev = to_cstor_udevice(pd->cdev);
	struct cstor_dealloc_pd_cmd cmd = {};
	int ret;

	cmd.pdid = pd->pdid;
	cstor_debug(ucdev, CSTOR_NOLOG, "pdid %u\n", pd->pdid);

	ret = cstor_ioctl(pd->cdev->dev_fd, CSTOR_IOCTL_DEALLOC_PD, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DEALLOC_PD cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	free(upd);
	return 0;
}

static __attribute__((constructor)) void cxgb4_register_driver(void)
{
	cstor_page_size = sysconf(_SC_PAGESIZE);
	cstor_page_mask = ~(cstor_page_size - 1);
}
