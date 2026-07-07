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

struct cstor_listen_sock *
cstor_create_listen(struct cstor_device *cdev, struct cstor_listen_attr *attr)
{
	struct cstor_udevice *ucdev;
	struct cstor_ulisten_sock *ulcsk;
	struct cstor_listen_sock *lcsk;
	struct cstor_create_listen_cmd cmd = {};
	int ret = ENODEV;

	if (!cdev) {
		cstor_printf(stderr, CSTOR_NOLOG, "cdev is not present\n");
		errno = ENODEV;
		return NULL;
	}

	if (attr->event_channel->cdev != cdev) {
		cstor_printf(stderr, CSTOR_NOLOG, "invalid cdev\n");
		errno = EINVAL;
		return NULL;
	}

	cmd.efd = attr->event_channel->efd;

	if (attr->laddr.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&attr->laddr;

		cmd.tcp_port = sin->sin_port;
		cmd.ip_addr[0] = sin->sin_addr.s_addr;
		cmd.ipv4 = 1;
	} else if (attr->laddr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&attr->laddr;

		cmd.tcp_port = sin6->sin6_port;
		memcpy(cmd.ip_addr, sin6->sin6_addr.s6_addr, 16);
	} else {
		cstor_printf(stderr, CSTOR_NOLOG, "invalid ip version\n");
		errno = EINVAL;
		return NULL;
	}

	switch (attr->protocol) {
	case CSTOR_NVME_TCP_PROTOCOL:
		cmd.protocol = _CSTOR_NVME_TCP_PROTOCOL;
		break;
	case CSTOR_ISCSI_PROTOCOL:
		cmd.protocol = _CSTOR_ISCSI_PROTOCOL;
		break;
	default:
		cstor_printf(stderr, CSTOR_NOLOG, "invalid protocol %u\n", attr->protocol);
		errno = EINVAL;
		return NULL;
	}

	ulcsk = calloc(1, sizeof(*ulcsk));
	if (!ulcsk) {
		cstor_printf(stderr, CSTOR_NOLOG, "failed to allocate ulcsk\n");
		errno = ENOMEM;
		return NULL;
	}

	cmd.first_pdu_recv_timeout = attr->first_pdu_recv_timeout;

	ucdev = to_cstor_udevice(cdev);
	pthread_mutex_lock(&ucdev->mlock);
	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_CREATE_LISTEN, &cmd);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_printf(stderr, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CREATE_LISTEN cmd, "
			     "ret %d\n", ret);
		free(ulcsk);
		errno = ret;
		return NULL;
	}

	ulcsk->refcnt++;
	ulcsk->ucdev = ucdev;

	lcsk = &ulcsk->lcsk;
	lcsk->port_id = cmd.resp.port_id;
	lcsk->cdev = &ucdev->cdev;
	lcsk->ctx = attr->ctx;
	lcsk->stid = cmd.resp.stid;
	lcsk->event_channel = attr->event_channel;
	if (cmd.ipv4) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&lcsk->laddr;

		sin->sin_family = AF_INET;
		sin->sin_port = cmd.tcp_port;
		sin->sin_addr.s_addr = cmd.ip_addr[0];
		cstor_debug(ucdev, CSTOR_NOLOG, "stid %u ulcsk->refcnt %u ipv4 port %d "
			    "address %s\n", lcsk->stid, ulcsk->refcnt,
			    be16toh(sin->sin_port), inet_ntoa(sin->sin_addr));
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&lcsk->laddr;
		char str[INET6_ADDRSTRLEN];

		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = cmd.tcp_port;
		memcpy(sin6->sin6_addr.s6_addr, cmd.ip_addr, 16);
		inet_ntop(AF_INET6, &sin6->sin6_addr, str, INET6_ADDRSTRLEN);
		cstor_debug(ucdev, CSTOR_NOLOG, "stid %u ulcsk->refcnt %u ipv6 port %d "
			    "address %s\n", lcsk->stid, ulcsk->refcnt,
			    be16toh(sin6->sin6_port), str);
	}

	set_listen_sock(ucdev, lcsk->stid, ulcsk);
	pthread_mutex_unlock(&ucdev->mlock);

	return lcsk;
}

static void cstor_put_listen_sock(struct cstor_listen_sock *lcsk)
{
	struct cstor_ulisten_sock *ulcsk = to_cstor_ulisten_sock(lcsk);
	struct cstor_udevice *ucdev = ulcsk->ucdev;

	if (!ulcsk->refcnt) {
		cstor_err(ucdev, CSTOR_NOLOG, "ulcsk->refcnt is 0\n");
		abort();
	}

	ulcsk->refcnt--;
	cstor_debug(ucdev, CSTOR_NOLOG, "stid %u ulcsk->refcnt %u\n",
		    lcsk->stid, ulcsk->refcnt);

	if (!ulcsk->refcnt) {
		set_listen_sock(ucdev, lcsk->stid, NULL);
		free(ulcsk);
	}
}

int cstor_destroy_listen(struct cstor_listen_sock *lcsk)
{
	struct cstor_ulisten_sock *ulcsk = to_cstor_ulisten_sock(lcsk);
	struct cstor_udevice *ucdev = ulcsk->ucdev;
	struct cstor_destroy_listen_cmd cmd = {};
	int ret;

	cmd.stid = lcsk->stid;

	cstor_debug(ucdev, CSTOR_NOLOG, "stid %u\n", cmd.stid);

	pthread_mutex_lock(&ucdev->mlock);
	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_DESTROY_LISTEN, &cmd);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_DESTROY_LISTEN cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	cstor_put_listen_sock(lcsk);
	pthread_mutex_unlock(&ucdev->mlock);

	return 0;
}

int cstor_sock_accept(struct cstor_sock *csk, struct cstor_sock_attr *attr)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_uqp *uqp = to_cstor_uqp(attr->qp);
	struct cstor_accept_cr_cmd cmd = {};
	int ret;

	cmd.tid = csk->tid;
	cmd.qid = attr->qp->qpid;
	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u qid %u\n", cmd.tid, cmd.qid);

	switch (attr->protocol) {
	case CSTOR_NVME_TCP_PROTOCOL:
		cmd.protocol = _CSTOR_NVME_TCP_PROTOCOL;
		cmd.attr.rx_pda = attr->nvme_tcp.rx_pda;
		cmd.attr.hdgst = attr->nvme_tcp.hdgst;
		cmd.attr.ddgst = attr->nvme_tcp.ddgst;
		cmd.attr.cmd_pdu_hdr_recv_zcopy = attr->nvme_tcp.cmd_pdu_hdr_recv_zcopy;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u rx_pda %u hdgst %u "
			    "ddgst %u cmd_pdu_hdr_recv_zcopy %u\n", attr->protocol,
			    cmd.attr.rx_pda, cmd.attr.hdgst, cmd.attr.ddgst,
			    cmd.attr.cmd_pdu_hdr_recv_zcopy);
		break;
	case CSTOR_ISCSI_PROTOCOL:
		cmd.protocol = _CSTOR_ISCSI_PROTOCOL;
		cmd.attr.ddp_page_size = attr->iscsi.ddp_page_size;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u iscsi_ddp_page_size %u\n",
			    attr->protocol, attr->iscsi.ddp_page_size);
		break;
	default:
		cstor_err(ucdev, CSTOR_NOLOG, "invalid protocol %u\n", attr->protocol);
		return EINVAL;
	}

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SOCK_ACCEPT, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SOCK_ACCEPT cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	uqp->qp.qp_enabled = 1;
	uqp->ucsk = ucsk;

	if (attr->protocol == CSTOR_ISCSI_PROTOCOL) {
		uqp->iscsi_ddp_page_size = attr->iscsi.ddp_page_size;
		cstor_debug(ucdev, CSTOR_NOLOG, "iscsi_ddp_page_size %u\n",
			    uqp->iscsi_ddp_page_size);
	}

	ucsk->uqp = uqp;

	return 0;
}

int cstor_sock_attach_qp(struct cstor_sock *csk, struct cstor_attach_qp_attr *attr)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_uqp *uqp = to_cstor_uqp(attr->qp);
	struct cstor_sock_attach_qp_cmd cmd = {};
	int ret;

	switch (attr->protocol) {
	case CSTOR_NVME_TCP_PROTOCOL:
		cmd.protocol = _CSTOR_NVME_TCP_PROTOCOL;
		cmd.attr.rx_pda = attr->nvme_tcp.rx_pda;
		cmd.attr.hdgst = attr->nvme_tcp.hdgst;
		cmd.attr.ddgst = attr->nvme_tcp.ddgst;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u rx_pda %u hdgst %u ddgst %u\n",
			    attr->protocol, cmd.attr.rx_pda, cmd.attr.hdgst, cmd.attr.ddgst);
		break;
	case CSTOR_ISCSI_PROTOCOL:
		cmd.protocol = _CSTOR_ISCSI_PROTOCOL;
		cmd.attr.ddp_page_size = attr->iscsi.ddp_page_size;
		cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u iscsi_ddp_page_size %u\n",
			    attr->protocol, attr->iscsi.ddp_page_size);
		break;
	default:
		cstor_err(ucdev, CSTOR_NOLOG, "invalid protocol %u\n", attr->protocol);
		return EINVAL;
	}

	cmd.qid = attr->qp->qpid;
	cmd.tid = csk->tid;

	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u qid %u\n", cmd.tid, cmd.qid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SOCK_ATTACH_QP, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SOCK_ATTACH_QP cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	uqp->qp.qp_enabled = 1;
	uqp->ucsk = ucsk;

	if (attr->protocol == CSTOR_ISCSI_PROTOCOL) {
		uqp->iscsi_ddp_page_size = attr->iscsi.ddp_page_size;
		cstor_debug(ucdev, CSTOR_NOLOG, "iscsi_ddp_page_size %u\n",
			    uqp->iscsi_ddp_page_size);
	}

	ucsk->uqp = uqp;

	return 0;
}

int ___cstor_sock_reject(struct cstor_udevice *ucdev, u32 tid)
{
	struct cstor_reject_cr_cmd cmd = {};
	int ret;

	cmd.tid = tid;
	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u\n", tid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SOCK_REJECT, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SOCK_REJECT cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

int cstor_sock_reject(struct cstor_sock *csk)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	int ret;

	pthread_mutex_lock(&ucdev->mlock);
	ret = ___cstor_sock_reject(ucdev, csk->tid);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_err(ucdev, CSTOR_NOLOG, "___cstor_sock_reject() failed, tid %u\n", csk->tid);
		return ret;
	}

	set_sock(ucdev, csk->tid, NULL);
	cstor_put_listen_sock(csk->lcsk);
	pthread_mutex_unlock(&ucdev->mlock);

	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u\n", csk->tid);
	free(ucsk);

	return 0;
}

struct cstor_sock *cstor_connect(struct cstor_device *cdev, struct cstor_connect_attr *attr)
{
	struct cstor_sock *csk;
	struct cstor_udevice *ucdev = to_cstor_udevice(cdev);
	struct cstor_usock *ucsk;
	struct cstor_connect_cmd cmd = {};
	int ret;

	switch (attr->protocol) {
	case CSTOR_NVME_TCP_PROTOCOL:
		cmd.protocol = _CSTOR_NVME_TCP_PROTOCOL;
		break;
	case CSTOR_ISCSI_PROTOCOL:
		cmd.protocol = _CSTOR_ISCSI_PROTOCOL;
		break;
	default:
		cstor_err(ucdev, CSTOR_NOLOG, "invalid protocol %u\n", attr->protocol);
		errno = EINVAL;
		return NULL;
	}

	cmd.raddr = attr->raddr;
	cmd.efd = attr->event_channel->efd;
	cstor_debug(ucdev, CSTOR_NOLOG, "protocol %u efd %d\n", attr->protocol, cmd.efd);

	ucsk = calloc(1, sizeof(struct cstor_usock));
	if (!ucsk) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucsk allocation\n");
		errno = ENOMEM;
		return NULL;
	}

	pthread_mutex_lock(&ucdev->mlock);
	ret = cstor_ioctl(cdev->dev_fd, CSTOR_IOCTL_CONNECT, &cmd);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_CONNECT cmd, "
			  "ret %d\n", ret);
		free(ucsk);
		errno = ret;
		return NULL;
	}

	ucsk->ucdev = ucdev;

	csk = &ucsk->csk;
	csk->atid = cmd.resp.atid;
	ucdev->atid2ptr[csk->atid] = ucsk;
	pthread_mutex_unlock(&ucdev->mlock);

	csk->raddr = attr->raddr;
	csk->event_channel = attr->event_channel;
	csk->ctx = attr->ctx;

	return csk;
}

struct cstor_device *cstor_resolve_route(struct sockaddr_storage raddr, u8 *port_id)
{
	struct cstor_udevice *ucdev;
	struct cstor_resolve_route_cmd cmd = {};
	int ret = ENODEV;

	cmd.raddr = raddr;

	list_for_each(&devices, ucdev, list) {
		ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_RESOLVE_ROUTE, &cmd);
		if (!ret) {
			*port_id = cmd.resp.port_id;
			return &ucdev->cdev;
		}
	}

	cstor_printf(stderr, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_RESOLVE_ROUTE cmd, "
		     "ret %d\n", ret);
	errno = ret;
	return NULL;
}

int cstor_sock_disconnect(struct cstor_sock *csk)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_sock_disconnect_cmd cmd = {};
	int ret;

	cmd.tid = csk->tid;
	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u\n", cmd.tid);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SOCK_DISCONNECT, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SOCK_DISCONNECT cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}

int cstor_sock_release(struct cstor_sock *csk)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_sock_release_cmd cmd = {};
	int ret;

	cmd.tid = csk->tid;
	cmd.atid = csk->atid;
	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u atid %u\n", cmd.tid, cmd.atid);

	pthread_mutex_lock(&ucdev->mlock);
	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_SOCK_RELEASE, &cmd);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_SOCK_RELEASE cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	if (csk->tid != CSTOR_INVALID_TID)
		set_sock(ucdev, csk->tid, NULL);

	if (csk->lcsk)
		cstor_put_listen_sock(csk->lcsk);

	if (csk->atid != CSTOR_INVALID_ATID)
		ucdev->atid2ptr[csk->atid] = NULL;

	pthread_mutex_unlock(&ucdev->mlock);

	free(ucsk);
	return 0;
}

static void cstor_fill_conn_info(struct cstor_usock *ucsk, struct _cstor_conn_info *conn_info)
{
	struct cstor_sock *csk = &ucsk->csk;

	csk->port_id = conn_info->port_id;
	csk->vlan_id = conn_info->vlan_id;
	ucsk->snd_nxt = conn_info->snd_nxt;
	ucsk->rcv_nxt = conn_info->rcv_nxt;
	cstor_debug(ucsk->ucdev, CSTOR_NOLOG, "tid %u port id %d vlan_id %u snd_nxt %#x "
		    "rcv_nxt %#x\n", csk->tid, csk->port_id, csk->vlan_id, ucsk->snd_nxt,
		    ucsk->rcv_nxt);

	if (conn_info->ipv4) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&csk->laddr;

		sin->sin_family = AF_INET;
		sin->sin_port = conn_info->lport;
		sin->sin_addr.s_addr = conn_info->laddr[0];
		cstor_debug(ucsk->ucdev, CSTOR_NOLOG, "ipv4 laddr port %d addr %s\n",
			    be16toh(sin->sin_port), inet_ntoa(sin->sin_addr));

		sin = (struct sockaddr_in *)&csk->raddr;
		sin->sin_family = AF_INET;
		sin->sin_port = conn_info->rport;
		sin->sin_addr.s_addr = conn_info->raddr[0];
		cstor_debug(ucsk->ucdev, CSTOR_NOLOG, "ipv4 raddr port %d addr %s\n",
			    be16toh(sin->sin_port), inet_ntoa(sin->sin_addr));
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&csk->laddr;
		char str[INET6_ADDRSTRLEN];

		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = conn_info->lport;
		memcpy(sin6->sin6_addr.s6_addr, conn_info->laddr, 16);
		inet_ntop(AF_INET6, &sin6->sin6_addr, str, INET6_ADDRSTRLEN);
		cstor_debug(ucsk->ucdev, CSTOR_NOLOG, "ipv6 laddr port %d addr %s\n",
			    be16toh(sin6->sin6_port), str);

		sin6 = (struct sockaddr_in6 *)&csk->raddr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = conn_info->rport;
		memcpy(sin6->sin6_addr.s6_addr, conn_info->raddr, 16);
		inet_ntop(AF_INET6, &sin6->sin6_addr, str, INET6_ADDRSTRLEN);
		cstor_debug(ucsk->ucdev, CSTOR_NOLOG, "ipv6 raddr port %d addr %s\n",
			    be16toh(sin6->sin6_port), str);
	}
}

int
cstor_process_connect_req_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				struct cstor_event *evt)
{
	struct cstor_sock *csk;
	struct cstor_ulisten_sock *ulcsk;
	struct cstor_usock *ucsk;
	struct _cstor_connect_req *req = &uevt->u.req;

	ucsk = calloc(1, sizeof(*ucsk));
	if (!ucsk) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ucsk allocation, tid %u\n", req->tid);
		___cstor_sock_reject(ucdev, req->tid);
		return ENOMEM;
	}

	ucsk->ucdev = ucdev;
	csk = &ucsk->csk;
	csk->tid = req->tid;
	csk->atid = CSTOR_INVALID_ATID;
	cstor_fill_conn_info(ucsk, &req->conn_info);

	ulcsk = get_listen_sock(ucdev, req->stid);
	if (unlikely(!ulcsk)) {
		cstor_err(ucdev, CSTOR_NOLOG, "ulcsk is NULL at stid %u\n", req->stid);
		abort();
	}

	csk->lcsk = &ulcsk->lcsk;

	pthread_mutex_lock(&ucdev->mlock);
	set_sock(ucdev, csk->tid, ucsk);
	ulcsk->refcnt++;
	pthread_mutex_unlock(&ucdev->mlock);

	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u port id %d vlan_id %u snd_nxt %#x rcv_nxt %#x\n",
		    csk->tid, csk->port_id, csk->vlan_id, ucsk->snd_nxt, ucsk->rcv_nxt);

	evt->event = CSTOR_EVENT_CONNECT_REQ;
	evt->u.req.csk = csk;

	return 0;
}

int cstor_free_atid(struct cstor_sock *csk)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_free_atid_cmd cmd = {};
	int ret;

	cmd.atid = csk->atid;
	cstor_debug(ucdev, CSTOR_NOLOG, "atid %u\n", csk->atid);

	pthread_mutex_lock(&ucdev->mlock);
	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_FREE_ATID, &cmd);
	if (ret) {
		pthread_mutex_unlock(&ucdev->mlock);
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_FREE_ATID cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	ucdev->atid2ptr[csk->atid] = NULL;
	csk->atid = CSTOR_INVALID_ATID;
	pthread_mutex_unlock(&ucdev->mlock);

	return 0;
}

int
cstor_process_connect_rpl_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				struct cstor_event *evt)
{
	struct cstor_sock *csk;
	struct cstor_usock *ucsk;
	struct _cstor_connect_rpl *rpl = &uevt->u.rpl;

	ucsk = ucdev->atid2ptr[rpl->atid];
	if (unlikely(!ucsk)) {
		cstor_err(ucdev, CSTOR_NOLOG, "ucsk is NULL at atid %u\n", rpl->atid);
		abort();
	}

	csk = &ucsk->csk;
	csk->tid = rpl->tid;
	cstor_fill_conn_info(ucsk, &rpl->conn_info);

	evt->event = CSTOR_EVENT_CONNECT_RPL;
	evt->u.rpl.csk = csk;
	if (rpl->status == _CSTOR_CONNECT_SUCCESS) {
		pthread_mutex_lock(&ucdev->mlock);
		set_sock(ucdev, csk->tid, ucsk);
		pthread_mutex_unlock(&ucdev->mlock);
		evt->u.rpl.status = CSTOR_CONNECT_SUCCESS;
	} else {
		csk->tid = CSTOR_INVALID_TID;
		evt->u.rpl.status = CSTOR_CONNECT_FAILURE;
	}

	return 0;
}

int
cstor_process_disconnected_event(struct cstor_udevice *ucdev, struct cstor_uevent *uevt,
				 struct cstor_event *evt)
{
	struct cstor_usock *ucsk;

	ucsk = get_sock(ucdev, uevt->u.tid);
	if (unlikely(!ucsk)) {
		cstor_err(ucdev, CSTOR_NOLOG, "get_sock() failed, tid %u\n", uevt->u.tid);
		abort();
	}

	evt->event = CSTOR_EVENT_DISCONNECTED;
	evt->u.csk = &ucsk->csk;

	return 0;
}

struct cstor_device *cstor_find_device(struct sockaddr_storage laddr, u8 *port_id)
{
	struct cstor_udevice *ucdev;
	struct cstor_find_device_cmd cmd = {};
	int ret = ENODEV;

	if (laddr.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&laddr;

		cmd.tcp_port = sin->sin_port;
		cmd.ip_addr[0] = sin->sin_addr.s_addr;
		cmd.ipv4 = 1;
	} else if (laddr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&laddr;
		char str[INET6_ADDRSTRLEN];

		cmd.tcp_port = sin6->sin6_port;
		memcpy(cmd.ip_addr, sin6->sin6_addr.s6_addr, 16);
		inet_ntop(AF_INET6, &sin6->sin6_addr, str, INET6_ADDRSTRLEN);
	} else {
		cstor_printf(stderr, CSTOR_NOLOG, "invalid ip version\n");
		errno = EINVAL;
		return NULL;
	}

	list_for_each(&devices, ucdev, list) {
		ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_FIND_DEVICE, &cmd);
		if (!ret) {
			*port_id = cmd.resp.port_id;
			return &ucdev->cdev;
		}
	}

	cstor_printf(stderr, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_FIND_DEVICE cmd, "
		     "ret %d\n", ret);
	errno = ret;
	return NULL;
}

int cstor_enable_iscsi_digest(struct cstor_sock *csk, struct cstor_iscsi_digest_attr *attr)
{
	struct cstor_usock *ucsk = to_cstor_usock(csk);
	struct cstor_udevice *ucdev = ucsk->ucdev;
	struct cstor_enable_iscsi_digest_cmd cmd = {};
	int ret;

	cmd.hdgst = attr->hdgst;
	cmd.ddgst = attr->ddgst;
	cmd.tid = csk->tid;

	cstor_debug(ucdev, CSTOR_NOLOG, "tid %u hdgst %u ddgst %u\n",
		    cmd.tid, cmd.hdgst, cmd.ddgst);

	ret = cstor_ioctl(ucdev->cdev.dev_fd, CSTOR_IOCTL_ENABLE_ISCSI_DIGEST, &cmd);
	if (ret) {
		cstor_err(ucdev, CSTOR_NOLOG, "failed ioctl CSTOR_IOCTL_ENABLE_ISCSI_DIGEST cmd, "
			  "ret %d\n", ret);
		return ret;
	}

	return 0;
}
