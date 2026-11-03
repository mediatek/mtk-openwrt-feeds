// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 MediaTek Inc. All Rights Reserved.
 *
 * Author: Chris Chou <chris.chou@mediatek.com>
 */

#include <linux/err.h>
#include <linux/module.h>

#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>
#include <mtk_eth_soc.h>

#include <pce/cls.h>
#include <pce/netsys.h>
#include <pce/pce.h>

#include "npu/mcu.h"
#include "npu/misc.h"
#include "npu/netsys.h"
#include "npu/tunnel.h"

#include "npu/protocol/tunnel/gre/gretap.h"
#include "npu/protocol/tunnel/l2tp/l2tpv2.h"
#include "npu/protocol/tunnel/pptp/pptp.h"
#include "npu/protocol/tunnel/vxlan/vxlan.h"

#include "npu-flow/internal.h"
#include "npu-flow/tnl-offload.h"

/*
 * check cls entry is updated for tunnel protocols that only use 1 CLS HW entry
 *
 * since only tunnel sync task will operate on tcls linked list,
 * it is safe to access without lock
 *
 * return true on updated
 * return false on need update
 */
static bool mtk_npu_flow_tnl_info_cls_is_updated(struct npu_tnl_info *tnl_info,
						 struct npu_tnl_type *tnl_type)
{
	struct npu_cls_entry *found = NULL;
	struct npu_cls_entry *tcls;

	/*
	 * check tnl_type has already allocate a npu_cls_entry
	 * if not, return false to prepare to allocate a new one
	 */
	if (list_empty(&tnl_type->tcls_head))
		return false;

	/*
	 * if tnl_info is not associate to tnl_type's cls entry,
	 * make a reference to npu_cls_entry
	 */
	if (!tnl_info->tcls) {
		list_for_each_entry(tcls, &tnl_type->tcls_head, node) {
			if (tcls->cls->cdesc.cdrt_idx == tnl_info->tnl_params.cdrt_idx) {
				found = tcls;
				break;
			}
		}

		if (!found)
			return false;

		mtk_npu_tnl_info_cls_link(tnl_info, found);
	}

	return tnl_info->tcls->updated;
}

static int mtk_npu_flow_tnl_info_cls_setup(struct npu_tnl_info *tnl_info)
{
	struct npu_tnl_params *tnl_params = &tnl_info->tnl_params;
	struct npu_tnl_type *tnl_type = tnl_info->tnl_type;
	struct npu_cls_entry *tcls;
	int ret;

	if (mtk_npu_flow_tnl_info_cls_is_updated(tnl_info, tnl_type))
		return 0;

	if (tnl_info->tcls)
		goto cls_entry_write;

	tcls = mtk_npu_tnl_info_cls_entry_alloc(tnl_info, tnl_params);
	if (IS_ERR(tcls))
		return PTR_ERR(tcls);

	/* TODO: set cdrt index for xxx over IPSec while learning */
	if (!tnl_params->cdrt_idx) {
		ret = tnl_type->cls_entry_setup(tnl_info, &tcls->cls->cdesc);
		if (ret) {
			NPU_NOTICE("npu cls entry setup failed: %d\n", ret);
			goto cls_entry_unprepare;
		}
	} else {
		/*
		 * since CLS is already filled up with outer protocol rule
		 * we only update CLS tport here to let matched packet to go through
		 * QDMA and specify the destination port to NPU
		 */
		CLS_DESC_DATA(&tcls->cls->cdesc, tport_idx, 3); /* EIP197 + QDMA */
		CLS_DESC_DATA(&tcls->cls->cdesc, fport, PSE_PORT_TDMA);
		CLS_DESC_DATA(&tcls->cls->cdesc, qid, 15);
	}

cls_entry_write:
	ret = mtk_npu_tnl_info_cls_entry_write(tnl_info);

cls_entry_unprepare:
	if (ret)
		mtk_npu_tnl_info_cls_entry_free(tnl_info, tnl_params);

	return ret;
}

static void mtk_npu_flow_tnl_info_cls_destroy(struct npu_tnl_info *tnl_info)
{
	struct npu_tnl_params *tnl_params = &tnl_info->tnl_params;
	struct npu_cls_entry *tcls = tnl_info->tcls;

	if (!tcls)
		return;

	if (!tnl_params->cdrt_idx)
		memset(&tcls->cls->cdesc, 0, sizeof(tcls->cls->cdesc));
	else {
		/* TODO: we should find correct ppe, since lan/wan swap is possible */
		CLS_DESC_DATA(&tcls->cls->cdesc, fport, PSE_PORT_PPE1);
		CLS_DESC_DATA(&tcls->cls->cdesc, tport_idx, 2);
		CLS_DESC_DATA(&tcls->cls->cdesc, qid, 0);
	}
}

static struct npu_tnl_info_ops flow_tnl_info_ops = {
	.cls_setup = mtk_npu_flow_tnl_info_cls_setup,
	.cls_destroy = mtk_npu_flow_tnl_info_cls_destroy,
};

static inline void mtk_npu_flow_tnl_info_preserve(struct npu_tnl_type *tnl_type,
						  struct npu_tnl_params *old,
						  struct npu_tnl_params *new)
{
	new->flag |= old->flag;
	new->cls_entry = old->cls_entry;

	/* restore mtu if old parameter already setup */
	if (old->params.mtu && old->params.mtu != tnl_type->max_mtu)
		new->params.mtu = old->params.mtu;
	else
		new->params.mtu = tnl_type->max_mtu;

	/* we can only get ttl from encapsulation */
	if (new->params.network.ip.ttl == 128 && old->params.network.ip.ttl != 0)
		new->params.network.ip.ttl = old->params.network.ip.ttl;

	/* call tunnel specific restore callback */
	if (tnl_type->tnl_param_restore)
		tnl_type->tnl_param_restore(&old->params, &new->params);
}

static int mtk_npu_flow_tnl_info_setup(struct npu_tnl_input *tnl_input,
				       struct npu_tnl_type *tnl_type,
				       struct npu_tnl_info *tnl_info,
				       struct npu_tnl_params *tnl_params)
{
	u32 *entry = tnl_input->flow.entry;
	bool has_diff = false;

	if (unlikely(!tnl_info || !tnl_params))
		return -EPERM;

	spin_lock(&tnl_info->lock);

	tnl_info->ops = &flow_tnl_info_ops;

	mtk_npu_flow_tnl_info_preserve(tnl_type, &tnl_info->cache, tnl_params);

	if (tnl_params->cdrt_idx != tnl_info->cache.cdrt_idx)
		mtk_npu_tnl_info_tcls_invalidate(tnl_info);

	has_diff = memcmp(&tnl_info->cache, tnl_params, sizeof(*tnl_params));
	if (has_diff) {
		memcpy(&tnl_info->cache, tnl_params, sizeof(*tnl_params));
		mtk_npu_tnl_info_hash_no_lock(tnl_info);
	}

	/* path->dev is accessed through const pointer, but net_device itself is not const */
	tnl_info->dev = (struct net_device *)tnl_input->flow.path->dev;
	tnl_info->cache.params.mtu = tnl_input->flow.path->dev->mtu;
	*entry = tnl_info->tnl_idx + __NPU_TUNNEL_TYPE_MAX;
	if (!tnl_info_decap_is_enable(tnl_info) || !tnl_info_encap_is_enable(tnl_info)) {
		has_diff = true;
		tnl_info_decap_enable(tnl_info);
		tnl_info_encap_enable(tnl_info);
	}

	if (has_diff)
		mtk_npu_tnl_info_submit_no_tnl_lock(tnl_info);

	spin_unlock(&tnl_info->lock);

	return 0;
}

static int mtk_npu_flow_tnl_offloadable(const struct net_device_path *path)
{
	if (!path)
		return 0;

	switch (path->type) {
	case DEV_PATH_L2TP:
	case DEV_PATH_PPTP:
		break;
	/* TODO: add other supported tunnel */
	default:
		return 0;
	}

	return 1;
}

static int mtk_npu_flow_tnl_offload(const struct net_device_path *path, u32 *entry)
{
	struct npu_tnl_input tnl_input;
	struct npu_tnl_params tnl_params;
	struct npu_tnl_type *tnl_type;
	struct xfrm_state *x, *encap;

	memset(&tnl_input, 0, sizeof(struct npu_tnl_input));
	memset(&tnl_params, 0, sizeof(struct npu_tnl_params));

	tnl_type = mtk_npu_tnl_type_get_by_path_type(path->type);
	if (IS_ERR(tnl_type))
		return PTR_ERR(tnl_type);

	if (unlikely(!tnl_type->tnl_flow_param_setup))
		return -ENODEV;

	tnl_type->tnl_flow_param_setup(path, &tnl_params.params);

	/* Try to find decap xfrm_state by encap state info */
	if (path->tunnel.dst) {
		encap = dst_xfrm(path->tunnel.dst);

		x = xfrm_state_lookup_byaddr(dev_net(path->tunnel.dst->dev), encap->mark.v,
					     &encap->props.saddr, &encap->id.daddr,
					     encap->id.proto, encap->props.family);
		if (!x)
			return -ENODEV;

		tnl_params.cdrt_idx = mtk_flow_offload_get_cdrt(x);
		xfrm_state_put(x);
	}

	tnl_params.npu_entry_proto = tnl_type->tnl_proto_type;
	tnl_input.flow.path = path;
	tnl_input.flow.entry = entry;

	return mtk_npu_tnl_offload(&tnl_input, tnl_type, &tnl_params, mtk_npu_flow_tnl_info_setup);
}

/* TODO: clear ppe entry with flowblock */
void mtk_npu_tnl_info_flush_all_ppe_tnl(void)
{
}

static struct net_device *mtk_npu_flow_get_tnl_dev(u8 npu_crsn)
{
	struct npu_tnl_info *tnl_info;
	u32 tnl_idx;

	if (npu_crsn < NPU_CRSN_TNL_ID_START || npu_crsn > NPU_CRSN_TNL_ID_END)
		return ERR_PTR(-EINVAL);

	tnl_idx = npu_crsn - NPU_CRSN_TNL_ID_START;

	tnl_info = mtk_npu_tnl_info_get_by_idx(tnl_idx);
	if (IS_ERR(tnl_info))
		return ERR_PTR(PTR_ERR(tnl_info));

	return tnl_info->dev;
}

static int mcu_netstop_pre_enter(struct mcu_state_notifier *notifier)
{
	mtk_npu_tnl_info_flush_all_ppe_tnl();

	mtk_npu_tnl_info_backup_all();

	return 0;
}

static int mcu_init_pre_leave(struct mcu_state_notifier *notifier)
{
	mtk_npu_misc_set_ppe_num();

	mtk_npu_tnl_info_restore_all();

	return 0;
}

static struct mcu_state_notifier mcu_netstop_notifier = {
	.pre_enter = mcu_netstop_pre_enter,
};

static struct mcu_state_notifier mcu_init_notifier = {
	.pre_leave = mcu_init_pre_leave,
};

int mtk_npu_flow_tnl_offload_init(void)
{
	mtk_npu_mcu_state_notifier_register(MCU_STATE_TYPE_NETSTOP, &mcu_netstop_notifier);

	mtk_npu_mcu_state_notifier_register(MCU_STATE_TYPE_INIT, &mcu_init_notifier);

	mtk_npu_gretap_init();

	mtk_npu_l2tpv2_init();

	mtk_npu_pptp_init();

	mtk_npu_vxlan_init();

	mtk_flow_tnl_offloadable = mtk_npu_flow_tnl_offloadable;
	mtk_flow_tnl_offload = mtk_npu_flow_tnl_offload;
	mtk_get_tnl_dev = mtk_npu_flow_get_tnl_dev;

	return 0;
}

void mtk_npu_flow_tnl_offload_deinit(void)
{
	mtk_npu_gretap_deinit();

	mtk_npu_l2tpv2_deinit();

	mtk_npu_pptp_deinit();

	mtk_npu_vxlan_deinit();

	mtk_flow_tnl_offloadable = NULL;
	mtk_flow_tnl_offload = NULL;
	mtk_get_tnl_dev = NULL;

	mtk_npu_tnl_info_flush_all_ppe_tnl();

	mtk_npu_mcu_state_notifier_unregister(MCU_STATE_TYPE_NETSTOP, &mcu_netstop_notifier);

	mtk_npu_mcu_state_notifier_unregister(MCU_STATE_TYPE_INIT, &mcu_init_notifier);
}
