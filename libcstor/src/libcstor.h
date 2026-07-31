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

#ifndef __LIBCSTOR_H__
#define __LIBCSTOR_H__

#include <linux/types.h>
#include <sys/socket.h>
#include <sys/eventfd.h>

typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

#define CSTOR_DEV_NAME_LEN 32
#define CSTOR_IFACE_NAME_LEN 16
#define CSTOR_FW_VER_LEN 32
#define CSTOR_MAX_PORTS 4
#define CSTOR_INVALID_NUMA_NODE_ID 0xffffffff

#define CSTOR_NVME_TCP_DDP_COLOR_BITS	4
#define CSTOR_NVME_TCP_NON_DDP_TAG_MASK 0x8000

#define CSTOR_MAX_IMM_PPOD_DATA_LEN	256
#define CSTOR_MAX_INVALIDATE_ISCSI_TAG 32
#define CSTOR_ISCSI_MAX_NON_DDP_TAG	((1U << 31) - 1)

struct cstor_device_attr {
	char name[CSTOR_DEV_NAME_LEN];
	char iface_name[CSTOR_MAX_PORTS][CSTOR_IFACE_NAME_LEN];
	char fw_ver[CSTOR_FW_VER_LEN];
	u64 mac_addr[CSTOR_MAX_PORTS];
	u64 max_mr_size;
	u64 page_size_cap;
	u32 fl_page_size_cap;
	u32 iscsi_page_size_cap;
	u32 vendor_id;
	u32 vendor_part_id;
	u32 numa_node_id;
	u32 hw_ver;
	u32 max_qp;
	u32 max_qp_wr;
	u32 max_send_sge;
	u32 max_rq_sge;
	u32 max_rxq_sge;
	u32 max_ddp_sge;
	u32 max_ddp_tag;
	u32 max_cq;
	u32 max_cqe;
	u32 max_mr;
	u32 max_pd;
	u32 max_srq;
	u32 max_srq_wr;
	u32 max_srq_sge;
	u32 max_send_imm_data;
	u32 max_lso_buf_size;
	u32 max_listen_sock;
	u32 max_sock;
	u32 num_ports;
	u32 wc_enabled;
};

struct cstor_device {
	char name[CSTOR_DEV_NAME_LEN];
	int dev_fd;
	u8 num_ports;
};

struct cstor_pd {
	struct cstor_device *cdev;
	u32 pdid;
};

enum cstor_access_flags {
	CSTOR_ACCESS_LOCAL_WRITE	= (1U << 0),
	CSTOR_ACCESS_REMOTE_WRITE	= (1U << 1),
	CSTOR_ACCESS_REMOTE_READ	= (1U << 2),
};

struct cstor_mr {
	struct cstor_pd *pd;
	void *addr;
	u64 length;
	u32 lkey;
};

struct cstor_cq_attr {
	void *ctx;
	int event_fd;
	u32 num_cqe;
	u8 no_lock;
};

struct cstor_cq {
	struct cstor_device *cdev;
	int event_fd;
	u32 cqid;
	u32 num_cqe;
};

struct cstor_srq_attr {
	u32 max_wr;
	u8 no_lock;
};

struct cstor_srq {
	struct cstor_pd *pd;
	u32 srqid;
};

struct cstor_qp_attr {
	void *ctx;
	struct cstor_cq *send_cq;
	struct cstor_cq *recv_cq;
	struct cstor_srq *srq;
	struct cstor_rxq *rxq;
	u32 max_send_wr;
	u32 max_recv_wr;
	u32 max_ddp_sge;
	u32 max_ddp_tag;
	u8 auto_cmpl;
	u8 no_lock;
	u8 protocol;
};

struct cstor_qp {
	void *ctx;
	struct cstor_pd *pd;
	struct cstor_cq	*send_cq;
	struct cstor_cq	*recv_cq;
	struct cstor_srq *srq;
	u32 qpid;
	u8 qp_enabled;
};

struct cstor_rxq_attr {
	u32 max_wr;
	u32 fl_page_size;
	u8 port_id;
	u8 no_lock;
};

struct cstor_rxq {
	struct cstor_device *cdev;
	u32 rxqid;
};

struct cstor_iscsi_ddp_tag_info {
	u16 pool_idx;
	u16 num_sge;
	u32 transfer_len;
	struct cstor_qp *qp;
	struct cstor_sge *ppod_sge;
	struct cstor_sge *sg_list;
};

#define CSTOR_INVALID_PORT_ID 0xff

struct cstor_listen_sock {
	struct cstor_device *cdev;
	struct cstor_event_channel *event_channel;
	void *ctx;
	struct sockaddr_storage laddr;
	u32 stid;
	u32 refcnt;
	u8 port_id;
};

struct cstor_sock {
	struct cstor_listen_sock *lcsk;
	struct cstor_event_channel *event_channel;
	void *ctx;
	struct sockaddr_storage laddr;
	struct sockaddr_storage raddr;
	u32 tid;
	u32 atid;
	u16 vlan_id;
	u8 port_id;
};

struct cstor_sge {
	u64 addr;
	u32 length;
	u32 lkey;
};

struct cstor_recv_wr {
	void *ctx;
	struct cstor_recv_wr *next;
	struct cstor_sge *sg_list;
	u8 num_sge;
};

enum cstor_send_opcode {
	CSTOR_SEND_OP_NVME_TCP_TX_PDU = 1,
	CSTOR_SEND_OP_NVME_TCP_LSO,
	CSTOR_SEND_OP_NVME_TCP_SETUP_DDP,
	CSTOR_SEND_OP_NVME_TCP_INVALIDATE_TAG,
	CSTOR_SEND_OP_ISCSI_TX_PDU,
	CSTOR_SEND_OP_ISCSI_SETUP_DDP,
	CSTOR_SEND_OP_ISCSI_INVALIDATE_TAG,
};

enum cstor_send_flags {
	CSTOR_SEND_FLAG_HDGST		= (1U << 0),
	CSTOR_SEND_FLAG_DDGST		= (1U << 1),
	CSTOR_SEND_FLAG_CMPL		= (1U << 2),
	CSTOR_SEND_FLAG_LAST_PDU	= (1U << 3),
};

struct cstor_send_wr {
	void *ctx;
	struct cstor_send_wr *next;
	struct cstor_sge *sg_list;
	void (*cb_fn)(void *cb_arg);
	u8 opcode;
	u8 flags;
	u8 num_sge;
	union {
		struct {
			u64 page_size;
			u32 r2t_offset;
			u16 ddp_tag;
			u8 num_pad_bytes;
		} nvme_tcp;
		struct {
			u32 ddp_tag;
		} iscsi;
	};
};

enum cstor_wc_opcode {
	CSTOR_WC_OP_NVME_TCP_PDU = 1,
	CSTOR_WC_OP_ISCSI_PDU,
	CSTOR_WC_OP_SEND_CMPL,
	CSTOR_WC_OP_SEND_ERR,
	CSTOR_WC_OP_INVALIDATE_TAG_CMPL,
	CSTOR_WC_OP_FLUSH,
};

enum cstor_nvme_tcp_status {
	CSTOR_NVME_TCP_SUCCESS = 0,
	CSTOR_NVME_TCP_HDGST_ERR = 1,
	CSTOR_NVME_TCP_DIR_ERR = 2,
	CSTOR_NVME_TCP_DGST_FLAG_ERR = 3,
	CSTOR_NVME_TCP_C2H_SUCCESS_BIT_ERR = 4,
	CSTOR_NVME_TCP_CMD_DATA_LEN_ERR = 5,
	CSTOR_NVME_TCP_UMODE_UNALLOC_ERR = 6,

	CSTOR_NVME_TCP_RQT_LIMIT_ERR = 8,
	CSTOR_NVME_TCP_RQT_WRAP_ERR = 9,
	CSTOR_NVME_TCP_RQT_SIZE_ERR = 10,

	CSTOR_NVME_TCP_TPT_LIMIT_ERR = 16,
	CSTOR_NVME_TCP_TPT_INVALID_ERR = 17,
	CSTOR_NVME_TCP_TPT_COLOR_ERR = 18,
	CSTOR_NVME_TCP_TPT_PROT_ERR = 19,
	CSTOR_NVME_TCP_TPT_WRAP_ERR = 20,
	CSTOR_NVME_TCP_TPT_BOUND_ERR = 21,
	CSTOR_NVME_TCP_TPT_LPDU_UNALIGNED_ERR = 22,

	CSTOR_NVME_TCP_PBL_LIMIT_ERR = 24,
	CSTOR_NVME_TCP_DDGST_ERR = 25,

	CSTOR_NVME_TCP_LEN_ERR = 33,
	CSTOR_NVME_TCP_SEQ_MISMATCH_ERR = 34,
	CSTOR_NVME_TCP_MAX_STATUS
};

struct cstor_nvme_tcp_wc {
	void *ctx;
	struct cstor_qp *qp;
	u32 seq;
	u8 opcode;
	u8 status:7;
	u8 data_ddp:1;
	u8 hlen;
	u8 hdr[76];
};

struct cstor_iscsi_wc {
	void *hctx;
	void *dctx;
	void *free_ctx;
	void *hdr;
	void *data;
	struct cstor_qp *qp;
	u32 seq;
	u32 hlen;
	u32 dlen;
	u32 status;
	u32 ddgst;
	u8 opcode;
	u8 data_ddp;
};

enum cstor_transport_protocol {
	CSTOR_NVME_TCP_PROTOCOL = 1,
	CSTOR_ISCSI_PROTOCOL = 2,
};

struct cstor_listen_attr {
	void *ctx;
	struct cstor_event_channel *event_channel;
	struct sockaddr_storage laddr;
	u32 first_pdu_recv_timeout;
	u8 protocol;
	u8 port_id;
};

struct cstor_iscsi_sock_attr {
	u32 ddp_page_size;
};

struct cstor_nvme_tcp_sock_attr {
	u8 rx_pda;
	u8 hdgst;
	u8 ddgst;
	u8 cmd_pdu_hdr_recv_zcopy;
};

struct cstor_sock_attr {
	struct cstor_qp *qp;
	union {
		struct cstor_iscsi_sock_attr iscsi;
		struct cstor_nvme_tcp_sock_attr nvme_tcp;
	};
	u8 protocol;
};

struct cstor_attach_qp_attr {
	struct cstor_qp *qp;
	union {
		struct cstor_nvme_tcp_sock_attr nvme_tcp;
		struct cstor_iscsi_sock_attr iscsi;
	};
	u8 protocol;
};

enum cstor_event_type {
	CSTOR_EVENT_CONNECT_REQ = 1,
	CSTOR_EVENT_CONNECT_RPL,
	CSTOR_EVENT_RECV_ISCSI_PDU,
	CSTOR_EVENT_DISCONNECTED,
	CSTOR_EVENT_DEVICE_FATAL,
};

struct cstor_connect_req {
	struct cstor_sock *csk;
};

struct cstor_connect_rpl {
	struct cstor_sock *csk;
	u8 status;
};

struct cstor_connect_attr {
	struct sockaddr_storage raddr;
	struct cstor_event_channel *event_channel;
	void *ctx;
	u8 protocol;
};

enum cstor_connect_status {
	CSTOR_CONNECT_SUCCESS = 1,
	CSTOR_CONNECT_FAILURE,
};

struct cstor_iscsi_pdu_info {
	struct cstor_sock *csk;
	u32 len;
	u32 hlen;
	u32 status;
};

struct cstor_event {
	void *buf;
	u32 buf_len;
	u32 event;
	union {
		struct cstor_connect_req req;
		struct cstor_connect_rpl rpl;
		struct cstor_iscsi_pdu_info pdu_info;
		struct cstor_sock *csk;
		u8 port_id;
	} u;
};

enum cstor_event_channel_flags {
	CSTOR_EVENT_CHANNEL_FLAG_NONBLOCK		= (1U << 0),
	CSTOR_EVENT_CHANNEL_FLAG_CM_EVENT		= (1U << 1),
	CSTOR_EVENT_CHANNEL_FLAG_ASYNC_EVENT		= (1U << 2),
};

struct cstor_event_channel {
	struct cstor_device *cdev;
	int efd;
};

struct cstor_iscsi_digest_attr {
	u8 hdgst;
	u8 ddgst;
};

			/* COMMON */

int cstor_open_devices(u32 *num_cdev);
void cstor_close_devices(void);
int cstor_get_devices(struct cstor_device **__cdev, u32 num_cdev);
int cstor_query_device(struct cstor_device *cdev, struct cstor_device_attr *attr);

struct cstor_pd *cstor_alloc_pd(struct cstor_device *cdev);
int cstor_dealloc_pd(struct cstor_pd *pd);

struct cstor_mr *
__cstor_reg_mr(struct cstor_sock *csk, struct cstor_pd *pd, void *addr, u64 length,
	       enum cstor_access_flags access);
struct cstor_mr *
cstor_reg_mr(struct cstor_pd *pd, void *addr, u64 length, enum cstor_access_flags access);
int cstor_dereg_mr(struct cstor_mr *mr);

struct cstor_listen_sock *
cstor_create_listen(struct cstor_device *cdev, struct cstor_listen_attr *attr);
int cstor_destroy_listen(struct cstor_listen_sock *lcsk);

struct cstor_qp *cstor_create_qp(struct cstor_pd *pd, struct cstor_qp_attr *attr);
int cstor_destroy_qp(struct cstor_qp *qp);
int cstor_post_send(struct cstor_qp *qp, struct cstor_send_wr *wr, struct cstor_send_wr **bad_wr);

int cstor_sock_accept(struct cstor_sock *csk, struct cstor_sock_attr *attr);
int cstor_sock_reject(struct cstor_sock *csk);
int cstor_free_atid(struct cstor_sock *csk);
int cstor_sock_attach_qp(struct cstor_sock *csk, struct cstor_attach_qp_attr *attr);
struct cstor_device *cstor_resolve_route(struct sockaddr_storage raddr, u8 *port_id);
struct cstor_sock *cstor_connect(struct cstor_device *cdev, struct cstor_connect_attr *attr);
int cstor_sock_disconnect(struct cstor_sock *csk);
int cstor_sock_release(struct cstor_sock *csk);
struct cstor_device *cstor_find_device(struct sockaddr_storage laddr, u8 *port_id);

int cstor_get_event(struct cstor_event_channel *event_channel, struct cstor_event *evt);
struct cstor_event_channel *cstor_create_event_channel(struct cstor_device *cdev, u8 flags);
int cstor_destroy_event_channel(struct cstor_event_channel *event_channel);

const char *cstor_get_send_status_str(u8 status);
u32 cstor_get_mdsl(struct cstor_device *cdev, u32 hlen, u32 pad_bytes, u8 hdgst_enabled,
		   u8 ddgst_enabled);

			/* NVMe/TCP */

struct cstor_cq *cstor_create_cq(struct cstor_device *cdev, struct cstor_cq_attr *attr);
int cstor_destroy_cq(struct cstor_cq *cq);
int cstor_poll_cq(struct cstor_cq *cq, struct cstor_nvme_tcp_wc *wc);
int cstor_arm_cq(struct cstor_cq *cq);

struct cstor_srq *cstor_create_srq(struct cstor_pd *pd, struct cstor_srq_attr *attr);
int cstor_destroy_srq(struct cstor_srq *srq);
int cstor_post_srq_recv(struct cstor_srq *srq, struct cstor_recv_wr *wr,
			struct cstor_recv_wr **bad_wr);
int cstor_post_rq_recv(struct cstor_qp *qp, struct cstor_recv_wr *wr,
		       struct cstor_recv_wr **bad_wr);

int cstor_alloc_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 *tag);
int cstor_realloc_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 *tag);
int cstor_free_nvme_tcp_ddp_tag(struct cstor_qp *qp, u16 tag);

const char *cstor_get_nvme_tcp_status_str(u8 status);

			/* iSCSI */

struct cstor_rxq *cstor_create_rxq(struct cstor_device *cdev, struct cstor_rxq_attr *attr);
int cstor_destroy_rxq(struct cstor_rxq *rxq);
int cstor_post_rxq_recv(struct cstor_rxq *rxq, struct cstor_recv_wr *wr,
			struct cstor_recv_wr **bad_wr);
int cstor_poll_rxq(struct cstor_rxq *rxq, struct cstor_iscsi_wc *wc);

int cstor_init_iscsi_ddp(struct cstor_device *cdev, u32 num_cores);
void cstor_release_iscsi_ddp(struct cstor_device *cdev);
u32 cstor_get_iscsi_ppod_buf_len(struct cstor_qp *qp, u64 first_page_addr, u32 transfer_len);
int cstor_alloc_iscsi_ddp_tag(struct cstor_iscsi_ddp_tag_info *tinfo, u32 *ddp_tag);
void cstor_get_iscsi_non_ddp_tag(u32 *tag);
int cstor_free_iscsi_ddp_tag(struct cstor_qp *qp, u32 ddp_tag);
int cstor_invalidate_iscsi_ddp_tag(struct cstor_qp *qp, u32 *tags, u32 num_tag);

int cstor_enable_iscsi_digest(struct cstor_sock *csk, struct cstor_iscsi_digest_attr *attr);

int cstor_send_iscsi_pdu(struct cstor_sock *csk, void *pdu, u32 len, u8 hdgst, u8 ddgst);

const char *cstor_get_iscsi_status_str(u8 idx);
#endif
