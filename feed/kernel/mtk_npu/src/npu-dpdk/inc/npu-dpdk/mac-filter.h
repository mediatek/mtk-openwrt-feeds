/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 MediaTek Inc. All Rights Reserved.
 *
 * Author: Ren-Ting Wang <ren-ting.wang@mediatek.com>
 */

#ifndef _NPU_DPDK_MAC_FILTER
#define _NPU_DPDK_MAC_FILTER

#include <linux/hashtable.h>
#include <linux/if_ether.h>
#include <linux/types.h>

#define NPU_DPDK_MAC_FILTER_NUM		CONFIG_MTK_NPU_DPDK_MAC_FILTER_NUM
#define NPU_DPDK_MAC_FILTER_MAP_BIT	6 /* 64 hash entry */

#define MAC_FILTER_LAN_PORT_NUM		10
#define MAC_FILTER_PORT_MASK		GENMASK(MAC_FILTER_FLAG_WIFI_BIT, MAC_FILTER_FLAG_WAN_BIT)
#define MAC_FILTER_PORT_WAN		MAC_FILTER_FLAG_WAN
#define MAC_FILTER_PORT_LAN(port)	MAC_FILTER_FLAG_LAN(port)

#define MAC_FILTER_FLAG_WAN		(BIT(MAC_FILTER_FLAG_WAN_BIT))
#define MAC_FILTER_FLAG_LAN(port)	(BIT(MAC_FILTER_FLAG_LAN_LSB + (port)))
#define MAC_FILTER_FLAG_WIFI		(BIT(MAC_FILTER_FLAG_WIFI_BIT))
#define MAC_FILTER_FLAG_EN		(BIT(MAC_FILTER_FLAG_EN_BIT))

enum mac_filter_flag_bit {
	MAC_FILTER_FLAG_WAN_BIT,
	MAC_FILTER_FLAG_LAN_LSB,
	MAC_FILTER_FLAG_LAN_MSB = MAC_FILTER_FLAG_LAN_LSB + MAC_FILTER_LAN_PORT_NUM - 1,
	MAC_FILTER_FLAG_WIFI_BIT,
	MAC_FILTER_FLAG_EN_BIT = 15,

	__MAC_FILTER_FLAG_MAX,
};

enum npu_mf_clear_type {
	NPU_MF_CLEAR_ALL,
	NPU_MF_CLEAR_MCAST,

	__NPU_MF_CLEAR_MAX,
};

enum npu_mf_config_type {
	NPU_MF_CONFIG_PORT,

	__NPU_MF_CONFIG_MAX,
};

enum npu_mf_query_type {
	NPU_MF_QUERY_ENTRY,
	NPU_MF_QUERY_AVAILABLE,

	__NPU_MF_QUERY_MAX,
};

struct npu_dpdk_mac_addr {
	u8 addr[ETH_ALEN];
	u16 flag;
} __aligned(4);

struct npu_dpdk_mac_info {
	struct npu_dpdk_mac_addr mac;
	u32 idx;
};

struct npu_dpdk_mf_entry {
	struct npu_dpdk_mac_addr mac;
	struct hlist_node node;
	u32 idx;
};

struct npu_dpdk_mf_clr_cmd {
	enum npu_mf_clear_type clr_type;
	u8 port_id;
};

struct npu_dpdk_mf_cfg {
	enum npu_mf_config_type cfg_type;
	struct {
		u8 id;
		bool enable;
	} port;
};

struct npu_dpdk_mf_query {
	enum npu_mf_query_type query_type;
	union {
		struct npu_dpdk_mac_info minfo;
		bool is_available;
	};
};

struct npu_dpdk_mac_filter_cmd {
	union {
		struct npu_dpdk_mac_info minfo;
		struct npu_dpdk_mf_clr_cmd clr_cmd;
		struct npu_dpdk_mf_cfg cfg;
		struct npu_dpdk_mf_query query;
	};
};

int mtk_npu_dpdk_mac_filter_entry_insert(struct npu_dpdk_mac_addr *mac);
int mtk_npu_dpdk_mac_filter_entry_delete(struct npu_dpdk_mac_addr *mac);
int mtk_npu_dpdk_mac_filter_entry_clear(const struct npu_dpdk_mf_clr_cmd *cmd);
int mtk_npu_dpdk_mac_filter_info_get_by_addr(struct npu_dpdk_mac_info *minfo,
					     struct npu_dpdk_mac_addr *mac);
int mtk_npu_dpdk_mac_filter_info_get_by_ofs(struct npu_dpdk_mac_info *minfo, u32 ofs);

int mtk_npu_dpdk_mac_filter_enable(bool en);
int mtk_npu_dpdk_mac_filter_config(const struct npu_dpdk_mf_cfg *cfg);
int mtk_npu_dpdk_mac_filter_entry_query(struct npu_dpdk_mf_query *query);
u32 mtk_npu_dpdk_mac_filter_drop_pkt_count(void);
int mtk_npu_dpdk_mac_filter_init(void);
void mtk_npu_dpdk_mac_filter_deinit(void);
#endif /* _NPU_DPDK_MAC_FILTER */
