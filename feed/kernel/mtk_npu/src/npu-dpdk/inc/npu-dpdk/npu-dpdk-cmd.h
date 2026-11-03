/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 MediaTek Inc. All Rights Reserved.
 *
 * Author: Ren-Ting Wang <ren-ting.wang@mediatek.com>
 */

#ifndef _NPU_DPDK_CMD_H_
#define _NPU_DPDK_CMD_H_

/* net command */
enum npu_dpdk_cmd {
	NPU_DPDK_NET_CMD_NULL,
	NPU_DPDK_NET_CMD_PARAMS,
	NPU_DPDK_NET_CMD_MAC_FILTER,
	NPU_DPDK_NET_CMD_SET_EN,

	__NPU_DPDK_NET_CMD_MAX,
};

enum npu_dpdk_params_cmd {
	NPU_DPDK_NET_CMD_MAC_FILTER_PARAMS_BASE_ADDR_GET,
	NPU_DPDK_NET_CMD_MAC_FILTER_PARAMS_DROP_PKT_GET,

	__NPU_DPDK_NET_CMD_PARAMS_MAX,
};

enum npu_mac_filter_cmd {
	NPU_MAC_FILTER_NET_CMD_UPDATE,
	NPU_MAC_FILTER_NET_CMD_DELETE,
	NPU_MAC_FILTER_NET_CMD_INVALIDATE,
	NPU_MAC_FILTER_NET_CMD_SET_EN,
	NPU_MAC_FILTER_NET_CMD_PORT_ENABLE,
	NPU_MAC_FILTER_NET_CMD_PORT_DISABLE,

	__NPU_MAC_FILTER_NET_CMD_MAX,
};

#endif /* _NPU_DPDK_CMD_H_ */
