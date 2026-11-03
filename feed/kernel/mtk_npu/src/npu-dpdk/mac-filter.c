// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 Mediatek Inc. All Rights Reserved.
 * Author: Ren-Ting Wang <ren-ting.wang@mediatek.com>
 */

#include <linux/mutex.h>

#include "npu/net-core.h"

#include "npu-dpdk/internal.h"
#include "npu-dpdk/mac-filter.h"
#include "npu-dpdk/npu-dpdk-cmd.h"

struct npu_mac_filter {
	struct npu_dpdk_mf_entry table[NPU_DPDK_MAC_FILTER_NUM];
	struct mutex lock;
	DECLARE_BITMAP(used, NPU_DPDK_MAC_FILTER_NUM);
	DECLARE_HASHTABLE(ht, NPU_DPDK_MAC_FILTER_MAP_BIT);
	u32 table_ofs;
};

static struct npu_mac_filter npu_mf;

static inline int npu_dpdk_mac_addr_fw_update(u32 idx)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_MAC_FILTER,
		.arg[0] = NPU_MAC_FILTER_NET_CMD_UPDATE,
		.arg[1] = idx,
	};

	return mtk_npu_net_send_cmd_mgmt(&cmd);
}

static inline void npu_dpdk_mac_addr_fw_invalidate(u32 idx)
{
	/* idx == NPU_DPDK_MAC_FILTER_NUM is a special "clear all" signal to FW */
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_MAC_FILTER,
		.arg[0] = NPU_MAC_FILTER_NET_CMD_INVALIDATE,
		.arg[1] = idx,
	};

	mtk_npu_net_send_cmd_offload_no_wait(&cmd);
}

static inline int npu_dpdk_mac_addr_fw_delete(u32 idx)
{
	/* idx == NPU_DPDK_MAC_FILTER_NUM is a special "clear all" signal to FW */
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_MAC_FILTER,
		.arg[0] = NPU_MAC_FILTER_NET_CMD_DELETE,
		.arg[1] = idx,
	};

	return mtk_npu_net_send_cmd_mgmt(&cmd);
}

static inline void npu_dpdk_mac_addr_fw_write(struct npu_dpdk_mac_addr *mac, u32 idx)
{
	u32 ofs = sizeof(struct npu_dpdk_mac_addr) * idx;
	u32 i;

	if (idx >= NPU_DPDK_MAC_FILTER_NUM)
		return;

	for (i = 0; i < sizeof(struct npu_dpdk_mac_addr); i += 4) {
		writel(*(u32 *)((uintptr_t)mac + i),
		       ndpdk.base + npu_mf.table_ofs + ofs + i);
	}
}

static inline void npu_dpdk_mac_addr_fw_clear(u32 idx)
{
	u32 ofs = sizeof(struct npu_dpdk_mac_addr) * idx;
	u32 i;

	if (idx >= NPU_DPDK_MAC_FILTER_NUM)
		return;

	for (i = 0; i < sizeof(struct npu_dpdk_mac_addr); i += 4)
		writel(0, ndpdk.base + npu_mf.table_ofs + ofs + i);
}

static inline u32 npu_dpdk_mac_hash(struct npu_dpdk_mac_addr *mac)
{
	u32 res = *((u32 *)&mac->addr[0]);

	res ^= (*((u16 *)&mac->addr[4])) << 16;
	res ^= *((u16 *)&mac->addr[4]);

	return res;
}

static struct npu_dpdk_mf_entry *mtk_npu_dpdk_mac_filter_entry_find(struct npu_dpdk_mac_addr *mac)
{
	struct npu_dpdk_mf_entry *entry;

	if (!mac)
		return ERR_PTR(-EINVAL);

	hash_for_each_possible(npu_mf.ht, entry, node, npu_dpdk_mac_hash(mac)) {
		if (!memcmp(entry->mac.addr, mac->addr, ETH_ALEN))
			return entry;
	}

	return ERR_PTR(-ENOENT);
}

static int mtk_npu_dpdk_mac_filter_entry_update(struct npu_dpdk_mf_entry *entry,
						struct npu_dpdk_mac_addr *new_mac)
{
	struct npu_dpdk_mac_addr orig_mac;
	int ret = 0;

	memcpy(&orig_mac, &entry->mac, sizeof(orig_mac));
	memcpy(&entry->mac, new_mac, sizeof(entry->mac));

	npu_dpdk_mac_addr_fw_write(&entry->mac, entry->idx);

	ret = npu_dpdk_mac_addr_fw_update(entry->idx);
	if (ret) {
		NPU_NOTICE("Mac filter entry update failed in firmware.\n");
		memcpy(&entry->mac, &orig_mac, sizeof(entry->mac));
		npu_dpdk_mac_addr_fw_write(&entry->mac, entry->idx);
		return ret;
	}

	npu_dpdk_mac_addr_fw_invalidate(entry->idx);

	return ret;
}

static int __mtk_npu_dpdk_mac_filter_entry_insert(struct npu_dpdk_mac_addr *mac)
{
	struct npu_dpdk_mf_entry *entry;
	int ret = 0;
	u32 i;

	i = find_first_zero_bit(npu_mf.used, NPU_DPDK_MAC_FILTER_NUM);

	if (i == NPU_DPDK_MAC_FILTER_NUM) {
		NPU_NOTICE("No available space to add new mac filter entry\n");
		return -ENOMEM;
	}

	npu_dpdk_mac_addr_fw_write(mac, i);

	ret = npu_dpdk_mac_addr_fw_update(i);
	if (ret) {
		NPU_NOTICE("mac filter entry: %pM update failed in firmware\n", mac->addr);
		npu_dpdk_mac_addr_fw_clear(i);
		return ret;
	}

	npu_dpdk_mac_addr_fw_invalidate(i);

	entry = &npu_mf.table[i];
	entry->idx = i;
	memcpy(&entry->mac, mac, sizeof(entry->mac));
	INIT_HLIST_NODE(&entry->node);
	hash_add(npu_mf.ht, &entry->node, npu_dpdk_mac_hash(&entry->mac));
	set_bit(i, npu_mf.used);

	return ret;
}

int mtk_npu_dpdk_mac_filter_entry_insert(struct npu_dpdk_mac_addr *mac)
{
	struct npu_dpdk_mf_entry *entry;
	int ret = 0;

	if (!mac || !mac->flag)
		return -EINVAL;

	mac->flag |= MAC_FILTER_FLAG_EN;

	mutex_lock(&npu_mf.lock);

	entry = mtk_npu_dpdk_mac_filter_entry_find(mac);
	if (!IS_ERR(entry)) {
		if ((entry->mac.flag & mac->flag) == mac->flag) {
			NPU_INFO("MAC address: %pM is already inserted to NPU MAC filter table\n",
				 mac->addr);
			goto unlock;
		}

		/* MAC exists with different flags, merge new flags in */
		mac->flag |= entry->mac.flag;
		ret = mtk_npu_dpdk_mac_filter_entry_update(entry, mac);
		goto unlock;
	}

	ret = __mtk_npu_dpdk_mac_filter_entry_insert(mac);

unlock:
	mutex_unlock(&npu_mf.lock);

	return ret;
}

static int __mtk_npu_dpdk_mac_filter_entry_delete(struct npu_dpdk_mf_entry *entry)
{
	int ret = 0;

	ret = npu_dpdk_mac_addr_fw_delete(entry->idx);
	if (ret) {
		NPU_INFO("mac filter entry delete failed in firmware\n");
		return ret;
	}

	npu_dpdk_mac_addr_fw_invalidate(entry->idx);

	npu_dpdk_mac_addr_fw_clear(entry->idx);

	clear_bit(entry->idx, npu_mf.used);
	hash_del(&entry->node);
	memset(entry, 0, sizeof(struct npu_dpdk_mf_entry));

	return ret;
}

int mtk_npu_dpdk_mac_filter_entry_delete(struct npu_dpdk_mac_addr *mac)
{
	struct npu_dpdk_mf_entry *entry;
	int ret = 0;
	u16 port;

	if (!mac)
		return -EINVAL;

	port = mac->flag & MAC_FILTER_PORT_MASK;
	if (!port)
		return 0;

	mutex_lock(&npu_mf.lock);

	entry = mtk_npu_dpdk_mac_filter_entry_find(mac);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto unlock;
	}

	/* Skip if none of the requested port bits exist in entry */
	if (!(entry->mac.flag & port))
		goto unlock;

	mac->flag = (entry->mac.flag & ~port);
	if (mac->flag != MAC_FILTER_FLAG_EN) {
		/* Other flags still active, just update FW */
		ret = mtk_npu_dpdk_mac_filter_entry_update(entry, mac);
		goto unlock;
	}

	/* No active flags remain, remove entry entirely */
	ret = __mtk_npu_dpdk_mac_filter_entry_delete(entry);

unlock:
	mutex_unlock(&npu_mf.lock);

	return ret;
}

static int mtk_npu_dpdk_mac_filter_entry_clear_mcast(const u8 port_id)
{
	struct npu_dpdk_mf_entry *entry;
	struct npu_dpdk_mac_addr tmp;
	unsigned long i;
	int ret = 0;
	u16 port;

	port = BIT(port_id) & MAC_FILTER_PORT_MASK;
	if (!port)
		return 0;

	mutex_lock(&npu_mf.lock);

	for_each_set_bit(i, npu_mf.used, NPU_DPDK_MAC_FILTER_NUM) {
		entry = &npu_mf.table[i];

		/* multicast: first byte LSB is 1 */
		if (!(entry->mac.addr[0] & 0x01))
			continue;

		/* Skip if none of the requested port bits exist in entry */
		if (!(entry->mac.flag & port))
			continue;

		memcpy(&tmp, &entry->mac, sizeof(tmp));
		tmp.flag &= ~port;

		if (tmp.flag != MAC_FILTER_FLAG_EN) {
			/* Other flags still active, just update FW */
			ret = mtk_npu_dpdk_mac_filter_entry_update(entry, &tmp);
			continue;
		}

		/* No active flags remain, remove entry entirely */
		ret = __mtk_npu_dpdk_mac_filter_entry_delete(entry);
	}

	mutex_unlock(&npu_mf.lock);

	return ret;
}

static int mtk_npu_dpdk_mac_filter_entry_clear_all(void)
{
	int ret = 0;
	u32 i;

	mutex_lock(&npu_mf.lock);

	ret = npu_dpdk_mac_addr_fw_delete(NPU_DPDK_MAC_FILTER_NUM);
	if (ret) {
		NPU_INFO("Mac filter entries clear failed in firmware.\n");
		goto unlock;
	}

	npu_dpdk_mac_addr_fw_invalidate(NPU_DPDK_MAC_FILTER_NUM);

	for (i = 0; i < NPU_DPDK_MAC_FILTER_NUM; ++i)
		npu_dpdk_mac_addr_fw_clear(i);

	hash_init(npu_mf.ht);
	bitmap_zero(npu_mf.used, NPU_DPDK_MAC_FILTER_NUM);
	memset(npu_mf.table, 0, sizeof(npu_mf.table));

unlock:
	mutex_unlock(&npu_mf.lock);

	return ret;
}

int mtk_npu_dpdk_mac_filter_entry_clear(const struct npu_dpdk_mf_clr_cmd *cmd)
{
	if (!cmd)
		return -EINVAL;

	switch (cmd->clr_type) {
	case NPU_MF_CLEAR_ALL:
		return mtk_npu_dpdk_mac_filter_entry_clear_all();
	case NPU_MF_CLEAR_MCAST:
		return mtk_npu_dpdk_mac_filter_entry_clear_mcast(cmd->port_id);
	default:
		return -EINVAL;
	}
}

int mtk_npu_dpdk_mac_filter_info_get_by_addr(struct npu_dpdk_mac_info *minfo,
					     struct npu_dpdk_mac_addr *mac)
{
	struct npu_dpdk_mf_entry *entry;

	if (!minfo || !mac)
		return -EINVAL;

	mutex_lock(&npu_mf.lock);

	entry = mtk_npu_dpdk_mac_filter_entry_find(mac);
	if (IS_ERR(entry)) {
		mutex_unlock(&npu_mf.lock);
		return PTR_ERR(entry);
	}

	minfo->idx = entry->idx;
	memcpy(&minfo->mac, &entry->mac, sizeof(minfo->mac));

	mutex_unlock(&npu_mf.lock);

	return 0;
}

int mtk_npu_dpdk_mac_filter_info_get_by_ofs(struct npu_dpdk_mac_info *minfo, u32 ofs)
{
	struct npu_dpdk_mf_entry *entry;
	u32 i;

	mutex_lock(&npu_mf.lock);

	i = find_next_bit(npu_mf.used, NPU_DPDK_MAC_FILTER_NUM, ofs);
	if (i == NPU_DPDK_MAC_FILTER_NUM) {
		mutex_unlock(&npu_mf.lock);
		return -ENOENT;
	}

	entry = &npu_mf.table[i];
	minfo->idx = entry->idx;
	memcpy(&minfo->mac, &entry->mac, sizeof(minfo->mac));

	mutex_unlock(&npu_mf.lock);

	return i;
}

static int mtk_npu_dpdk_mac_filter_base_addr_init(void)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_PARAMS,
		.arg[0] = NPU_DPDK_NET_CMD_MAC_FILTER_PARAMS_BASE_ADDR_GET,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt != 1)
		return -EINVAL;
	npu_mf.table_ofs = cmd.ret[0] - 0x09100000;

	return ret;
}

int mtk_npu_dpdk_mac_filter_enable(bool en)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_MAC_FILTER,
		.arg[0] = NPU_MAC_FILTER_NET_CMD_SET_EN,
		.arg[1] = en,
	};

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return 0;
}

static int mtk_npu_dpdk_mac_filter_port_en(u8 port, bool en)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_MAC_FILTER,
		.arg[0] = en ? NPU_MAC_FILTER_NET_CMD_PORT_ENABLE :
			       NPU_MAC_FILTER_NET_CMD_PORT_DISABLE,
		.arg[1] = port,
	};

	if (port > MAC_FILTER_LAN_PORT_NUM)
		return -EINVAL;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return 0;
}

int mtk_npu_dpdk_mac_filter_config(const struct npu_dpdk_mf_cfg *cfg)
{
	if (!cfg)
		return -EINVAL;

	switch (cfg->cfg_type) {
	case NPU_MF_CONFIG_PORT:
		return mtk_npu_dpdk_mac_filter_port_en(cfg->port.id, cfg->port.enable);
	default:
		return -EINVAL;
	}

}

static int mtk_npu_dpdk_mac_filter_is_table_available(struct npu_dpdk_mf_query *query)
{
	int i;

	mutex_lock(&npu_mf.lock);
	i = find_first_zero_bit(npu_mf.used, NPU_DPDK_MAC_FILTER_NUM);
	mutex_unlock(&npu_mf.lock);

	query->is_available = (i != NPU_DPDK_MAC_FILTER_NUM);

	return 0;
}

int mtk_npu_dpdk_mac_filter_entry_query(struct npu_dpdk_mf_query *query)
{
	if (!query)
		return -EINVAL;

	switch (query->query_type) {
	case NPU_MF_QUERY_ENTRY:
		return mtk_npu_dpdk_mac_filter_info_get_by_addr(&query->minfo, &query->minfo.mac);
	case NPU_MF_QUERY_AVAILABLE:
		return mtk_npu_dpdk_mac_filter_is_table_available(query);
	default:
		return -EINVAL;
	}
}

u32 mtk_npu_dpdk_mac_filter_drop_pkt_count(void)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_DPDK,
		.sub_type = NPU_DPDK_NET_CMD_PARAMS,
		.arg[0] = NPU_DPDK_NET_CMD_MAC_FILTER_PARAMS_DROP_PKT_GET,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt != NET_RET_CNT_1)
		return 0;

	return cmd.ret[0];
}

int mtk_npu_dpdk_mac_filter_init(void)
{
	int ret;

	mutex_init(&npu_mf.lock);

	ret = mtk_npu_dpdk_mac_filter_base_addr_init();
	if (ret) {
		NPU_NOTICE("dpdk mac filter base addr set failed: %d\n", ret);
		return ret;
	}

	ret = mtk_npu_dpdk_mac_filter_enable(true);
	if (ret) {
		NPU_NOTICE("dpdk mac filter set failed: %d\n", ret);
		return ret;
	}

	return ret;
}

void mtk_npu_dpdk_mac_filter_deinit(void)
{
	mtk_npu_dpdk_mac_filter_enable(false);

	mtk_npu_dpdk_mac_filter_entry_clear_all();
}
