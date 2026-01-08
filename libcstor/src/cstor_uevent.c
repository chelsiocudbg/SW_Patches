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
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#include "cstor_umain.h"

static int
cstor_process_device_fatal_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				 struct cstor_event *evt)
{
	evt->event = CSTOR_EVENT_DEVICE_FATAL;
	return 0;
}

static int
cstor_process_recv_iscsi_pdu(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
			     struct cstor_event *evt)
{
	struct cstor_usock *ucsk;
	struct _cstor_iscsi_pdu_info *pdu_info = &uevt->u.pdu_info;

	evt->event = CSTOR_EVENT_RECV_ISCSI_PDU;
	evt->u.pdu_info.len = pdu_info->pdu_len;
	evt->u.pdu_info.hlen = pdu_info->hlen;
	evt->u.pdu_info.status = pdu_info->status;
	cstor_debug(ucdev, CSTOR_NOLOG, "len %u hlen %u status %u\n",
		    evt->u.pdu_info.len, evt->u.pdu_info.hlen, evt->u.pdu_info.status);

	ucsk = get_sock(ucdev, pdu_info->tid);
	if (!ucsk) {
		cstor_err(ucdev, CSTOR_NOLOG, "get_sock() disabled, tid %u\n", pdu_info->tid);
		abort();
	}

	evt->u.pdu_info.csk = &ucsk->csk;
	return 0;
}

typedef int (*cstor_uevent_handler_t)(struct cstor_udevice *, struct cstor_uevent *,
				      struct cstor_event *);

cstor_uevent_handler_t cstor_evt_handler[CSTOR_UEVENT_MAX] = {
	[CSTOR_UEVENT_CONNECT_REQ] = cstor_process_connect_req_event,
	[CSTOR_UEVENT_CONNECT_RPL] = cstor_process_connect_rpl_event,
	[CSTOR_UEVENT_RECV_ISCSI_PDU] = cstor_process_recv_iscsi_pdu,
	[CSTOR_UEVENT_DISCONNECTED] = cstor_process_disconnected_event,
	[CSTOR_UEVENT_DEVICE_FATAL] = cstor_process_device_fatal_event,
};

int cstor_get_event(struct cstor_event_channel *event_channel, struct cstor_event *evt)
{
	struct cstor_device *cdev = event_channel->cdev;
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_uevent *uevt;
	struct cstor_get_uevent_cmd cmd = {};
	eventfd_t value;
	int ret;

	cmd.efd = event_channel->efd;
	cmd.buf = (uintptr_t)(evt->buf);
	cmd.buf_len = evt->buf_len;
	cstor_debug(ucdev, CSTOR_NOLOG, "efd %u buf_len %u\n", cmd.efd, cmd.buf_len);

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_GET_UEVENT, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_GET_UEVENT cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	eventfd_read(event_channel->efd, &value);
	uevt = &cmd.resp.uevt;
	ret = cstor_evt_handler[uevt->event](ucdev, uevt, evt);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "cstor_evt_handler[%u]() failed, ret %d\n",
			  uevt->event, ret);
		return ret;
	}

	return 0;
}

int cstor_destroy_event_channel(struct cstor_event_channel *event_channel)
{
	struct cstor_uevent_channel *uevent_channel = to_cstor_uevent_channel(event_channel);
	struct cstor_udevice *ucdev = to_cstor_udevice(event_channel->cdev);
	struct cstor_destroy_event_channel_cmd cmd = {};
	int ret;

	cmd.efd = event_channel->efd;
	cstor_debug(ucdev, CSTOR_NOLOG, "cmd.efd %u\n", cmd.efd);

	ret = cstor_ioctl(event_channel->cdev->dev_fd, CSTOR_IOCTL_DESTROY_EVENT_CHANNEL, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl "
			  "CSTOR_IOCTL_DESTROY_EVENT_CHANNEL cmd, ret %d\n", ret);
		return ret;
	}

	close(event_channel->efd);
	free(uevent_channel);
	return 0;
}

struct cstor_event_channel *cstor_create_event_channel(struct cstor_device *cdev, u8 flags)
{
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_uevent_channel *uevent_channel;
	struct cstor_event_channel *event_channel;
	struct cstor_create_event_channel_cmd cmd = {};
	int event_fd_flags = EFD_SEMAPHORE;
	int ret;

	uevent_channel = calloc(1, sizeof(struct cstor_uevent_channel));
	if (!uevent_channel) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed uevent_channel allocation\n");
		errno = ENOMEM;
		return NULL;
	}

	if (flags & CSTOR_EVENT_CHANNEL_FLAG_NONBLOCK)
		event_fd_flags |= EFD_NONBLOCK;

	event_channel = &uevent_channel->event_channel;
	event_channel->efd = eventfd(0, event_fd_flags);
	if (event_channel->efd < 0) {
		ret = errno;
		cstor_err(ucdev, CSTOR_NOLOG, "eventfd() failed, event_channel->efd %d, errno %d\n",
			  event_channel->efd, ret);
		free(uevent_channel);
		errno = ret;
		return NULL;
	}

	event_channel->cdev = cdev;

	cmd.efd = event_channel->efd;

	if (flags & CSTOR_EVENT_CHANNEL_FLAG_CM_EVENT)
		cmd.flag |= _CSTOR_EVENT_CHANNEL_FLAG_CM_EVENT;

	if (flags & CSTOR_EVENT_CHANNEL_FLAG_ASYNC_EVENT)
		cmd.flag |= _CSTOR_EVENT_CHANNEL_FLAG_ASYNC_EVENT;

	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_CREATE_EVENT_CHANNEL, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_EVENT_CHANNEL cmd, "
			  "ret %d\n", ret);
		close(event_channel->efd);
		free(uevent_channel);
		errno = ret;
		return NULL;
	}

	return event_channel;
}
