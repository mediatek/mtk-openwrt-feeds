// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Ren-Ting Wang <ren-ting.wang@mediatek.com>
 */

#include <linux/err.h>
#include <linux/module.h>

#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>

#include <mtk_eth_soc.h>
#include <mtk_hnat/hnat.h>
#include <mtk_hnat/nf_hnat_mtk.h>

#include <pce/cls.h>
#include <pce/netsys.h>
#include <pce/pce.h>

#include "npu/mcu.h"
#include "npu/misc.h"
#include "npu/netsys.h"

#include "npu/protocol/tunnel/gre/gretap.h"
#include "npu/protocol/tunnel/l2tp/l2tpv2.h"
#include "npu/protocol/tunnel/pptp/pptp.h"
#include "npu/protocol/tunnel/vxlan/vxlan.h"

#include "npu-nf_hnat/internal.h"
#include "npu-nf_hnat/tnl-offload.h"

static int mtk_npu_nf_hnat_tnl_info_cls_setup(struct npu_tnl_info *tnl_info);
static void mtk_npu_nf_hnat_tnl_info_cls_destroy(struct npu_tnl_info *tnl_info);

static struct npu_tnl_info_ops hnat_tnl_info_ops = {
	.cls_setup = mtk_npu_nf_hnat_tnl_info_cls_setup,
	.cls_destroy = mtk_npu_nf_hnat_tnl_info_cls_destroy,
};
static struct npu_nf_hnat_statistic hnat_ts;

static inline void inc_tnl_hnat_decap_offloadable_statistic_success(void)
{
	hnat_ts.decap_offloadable.success++;
}

static inline void inc_tnl_hnat_decap_offloadable_statistic_fail(void)
{
	hnat_ts.decap_offloadable.fail++;
}

static inline void inc_tnl_hnat_decap_offload_statistic_success(void)
{
	hnat_ts.decap_offload.success++;
}

static inline void inc_tnl_hnat_decap_offload_statistic_fail(void)
{
	hnat_ts.decap_offload.fail++;
}

static inline void inc_tnl_hnat_encap_offload_statistic_success(void)
{
	hnat_ts.encap_offload.success++;
}

static inline void inc_tnl_hnat_encap_offload_statistic_fail(void)
{
	hnat_ts.encap_offload.fail++;
}

static inline void skb_set_npu_tnl_idx(struct sk_buff *skb, u32 tnl_idx)
{
	skb_hnat_tops(skb) = tnl_idx + __NPU_TUNNEL_TYPE_MAX;
}

static inline bool skb_npu_valid(struct sk_buff *skb)
{
	return (skb && skb_hnat_tops(skb) < __NPU_TUNNEL_TYPE_MAX);
}

static inline struct npu_tnl_type *skb_to_tnl_type(struct sk_buff *skb)
{
	enum npu_tunnel_type tnl_proto_type = skb_hnat_tops(skb);

	if (unlikely(!tnl_proto_type || tnl_proto_type >= __NPU_TUNNEL_TYPE_MAX))
		return ERR_PTR(-EINVAL);

	return mtk_npu_tnl_type_get_by_idx(tnl_proto_type);
}

static inline struct npu_tnl_info *skb_to_tnl_info(struct sk_buff *skb)
{
	u32 tnl_idx = skb_hnat_tops(skb) - __NPU_TUNNEL_TYPE_MAX;

	return mtk_npu_tnl_info_get_by_idx(tnl_idx);
}

static inline void skb_mark_unbind(struct sk_buff *skb)
{
	skb_hnat_tops(skb) = 0;
	skb_hnat_is_decap(skb) = 0;
	skb_hnat_alg(skb) = 1;
}

/*
 * check cls entry is updated for tunnel protocols that only use 1 CLS HW entry
 *
 * since only tunnel sync task will operate on tcls linked list,
 * it is safe to access without lock
 *
 * return true on updated
 * return false on need update
 */
static bool mtk_npu_nf_hnat_tnl_info_cls_is_updated(struct npu_tnl_info *tnl_info,
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

static int mtk_npu_nf_hnat_tnl_info_cls_setup(struct npu_tnl_info *tnl_info)
{
	struct npu_tnl_params *tnl_params = &tnl_info->tnl_params;
	struct npu_tnl_type *tnl_type = tnl_info->tnl_type;
	struct npu_cls_entry *tcls;
	int ret;

	if (mtk_npu_nf_hnat_tnl_info_cls_is_updated(tnl_info, tnl_type))
		return 0;

	if (tnl_info->tcls)
		goto cls_entry_write;

	tcls = mtk_npu_tnl_info_cls_entry_alloc(tnl_info, tnl_params);
	if (IS_ERR(tcls))
		return PTR_ERR(tcls);

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
		CLS_DESC_DATA(&tcls->cls->cdesc, tport_idx, NR_EIP197_QDMA_TPORT);
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

static void mtk_npu_nf_hnat_tnl_info_cls_destroy(struct npu_tnl_info *tnl_info)
{
	struct npu_tnl_params *tnl_params = &tnl_info->tnl_params;
	struct npu_cls_entry *tcls = tnl_info->tcls;

	if (!tcls)
		return;

	if (!tnl_params->cdrt_idx)
		memset(&tcls->cls->cdesc, 0, sizeof(tcls->cls->cdesc));
	else
		/*
		 * recover tport_ix to let matched-packets to
		 * go through EIP197 only
		 */
		CLS_DESC_DATA(&tcls->cls->cdesc, tport_idx, 2);
}

static void mtk_npu_nf_hnat_flush_ppe_tnl_entry(struct foe_entry *entry, u32 tnl_idx)
{
	u32 bind_tnl_idx;

	if (unlikely(!entry))
		return;

	switch (entry->bfib1.pkt_type) {
	case IPV4_HNAPT:
		if (entry->ipv4_hnapt.tport_id != NR_TDMA_TPORT
		    &&  entry->ipv4_hnapt.tport_id != NR_TDMA_QDMA_TPORT)
			return;

		bind_tnl_idx = entry->ipv4_hnapt.tops_entry - __NPU_TUNNEL_TYPE_MAX;

		break;
	default:
		return;
	}

	/* unexpected tunnel index */
	if (bind_tnl_idx >= __NPU_TUNNEL_TYPE_MAX)
		return;

	if (tnl_idx == __NPU_TUNNEL_TYPE_MAX || tnl_idx == bind_tnl_idx)
		memset(entry, 0, sizeof(*entry));
}

void mtk_npu_tnl_info_flush_one_ppe_tnl_no_lock(struct npu_tnl_info *tnl_info)
{
	struct foe_entry *entry;
	u32 max_entry;
	u32 ppe_id;
	u32 eidx;

	if (unlikely(!tnl_info))
		return;

	/* tnl info's lock should be held */
	lockdep_assert_held(&tnl_info->lock);

	/* clear all NPU related PPE entries */
	for (ppe_id = 0; ppe_id < MAX_PPE_NUM; ppe_id++) {
		max_entry = mtk_npu_netsys_ppe_get_max_entry_num(ppe_id);
		for (eidx = 0; eidx < max_entry; eidx++) {
			entry = hnat_get_foe_entry(ppe_id, eidx);
			if (IS_ERR(entry))
				break;

			if (!entry_hnat_is_bound(entry))
				continue;

			mtk_npu_nf_hnat_flush_ppe_tnl_entry(entry, tnl_info->tnl_idx);
		}
	}

	hnat_cache_ebl(1);

	/* make sure all data is written to dram PPE table */
	wmb();
}

void mtk_npu_tnl_info_flush_one_ppe_tnl(struct npu_tnl_info *tnl_info)
{
	unsigned long flag = 0;

	if (unlikely(!tnl_info))
		return;

	spin_lock_irqsave(&tnl_info->lock, flag);

	mtk_npu_tnl_info_flush_one_ppe_tnl_no_lock(tnl_info);

	spin_unlock_irqrestore(&tnl_info->lock, flag);
}

void mtk_npu_tnl_info_flush_all_ppe_tnl(void)
{
	struct foe_entry *entry;
	u32 max_entry;
	u32 ppe_id;
	u32 eidx;

	/* clear all NPU related PPE entries */
	for (ppe_id = 0; ppe_id < MAX_PPE_NUM; ppe_id++) {
		max_entry = mtk_npu_netsys_ppe_get_max_entry_num(ppe_id);
		for (eidx = 0; eidx < max_entry; eidx++) {
			entry = hnat_get_foe_entry(ppe_id, eidx);
			if (IS_ERR(entry))
				break;

			if (!entry_hnat_is_bound(entry))
				continue;

			mtk_npu_nf_hnat_flush_ppe_tnl_entry(entry, __NPU_TUNNEL_TYPE_MAX);
		}
	}

	hnat_cache_ebl(1);

	/* make sure all data is written to dram PPE table */
	wmb();
}

static inline void mtk_npu_nf_hnat_tnl_info_preserve(struct npu_tnl_type *tnl_type,
						     struct npu_tnl_params *old,
						     struct npu_tnl_params *new)
{
	new->flag |= old->flag;
	new->cls_entry = old->cls_entry;
	if (old->cdrt_idx)
		new->cdrt_idx = old->cdrt_idx;

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

/* tnl_info->lock should be held before calling this function */
static int mtk_npu_nf_hnat_tnl_info_setup(struct npu_tnl_input *tnl_input,
					  struct npu_tnl_type *tnl_type,
					  struct npu_tnl_info *tnl_info,
					  struct npu_tnl_params *tnl_params)
{
	struct sk_buff *skb = tnl_input->hnat.skb;
	bool has_diff = false;

	if (unlikely(!skb || !tnl_info || !tnl_params))
		return -EPERM;

	spin_lock(&tnl_info->lock);

	tnl_info->ops = &hnat_tnl_info_ops;

	mtk_npu_nf_hnat_tnl_info_preserve(tnl_type, &tnl_info->cache, tnl_params);

	has_diff = memcmp(&tnl_info->cache, tnl_params, sizeof(*tnl_params));
	if (has_diff) {
		memcpy(&tnl_info->cache, tnl_params, sizeof(*tnl_params));
		mtk_npu_tnl_info_hash_no_lock(tnl_info);
	}

	if (skb_hnat_is_decap(skb)) {
		/* the net_device is used to forward pkt to decap'ed inf when Rx */
		tnl_info->dev = skb->dev;
		tnl_info->cache.params.mtu = READ_ONCE(skb->dev->mtu);
		if (!tnl_info_decap_is_enable(tnl_info)) {
			has_diff = true;
			tnl_info_decap_enable(tnl_info);
		}
	} else if (skb_hnat_is_encap(skb)) {
		/* set skb_hnat_tops(skb) to tunnel index for ppe binding */
		skb_set_npu_tnl_idx(skb, tnl_info->tnl_idx);
		if (!tnl_info_encap_is_enable(tnl_info)) {
			has_diff = true;
			tnl_info_encap_enable(tnl_info);
		}
	}

	if (has_diff)
		mtk_npu_tnl_info_submit_no_tnl_lock(tnl_info);

	spin_unlock(&tnl_info->lock);

	return 0;
}

static bool mtk_npu_tnl_decap_offloadable(struct sk_buff *skb)
{
	struct flow_offload_hw_path hw_path = { 0 };
	struct npu_tnl_type *tnl_type;
	struct iphdr *iph;
	u32 offload_tnl_type_num;
	u32 cnt;
	u32 i;

	/* only allow packets decrypted by xfrm packet offload */
	if (secpath_exists(skb)) {
		struct sec_path *sp = skb_sec_path(skb);

		for (i = 0; i < sp->len; i++)
			if (sp->xvec[i]->xso.type != XFRM_DEV_OFFLOAD_PACKET)
				return false;
	}

	if (unlikely(!mtk_npu_mcu_alive())) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_decap_offloadable_statistic_fail();
		return -EAGAIN;
	}

	/* skb should not carry npu here */
	if (skb_hnat_tops(skb)) {
		inc_tnl_hnat_decap_offloadable_statistic_fail();
		return false;
	}

	iph = ip_hdr(skb);
	/* TODO: currently decap only support ipv4 */
	if (iph->version != 4)
		return false;

	hw_path.dev = skb->dev;
	hw_path.virt_dev = skb->dev;

	if (skb->dev->netdev_ops->ndo_flow_offload_check) {
		skb->dev->netdev_ops->ndo_flow_offload_check(&hw_path);
		if (hw_path.flags & BIT(DEV_PATH_PPPOE))
			skb_hnat_set_is_pppoe(skb, 1);
	}

	offload_tnl_type_num = mtk_npu_tnl_type_get_offload_num();
	/* TODO: may can be optimized */
	for (i = NPU_TUNNEL_GRETAP, cnt = 0;
	     i < __NPU_TUNNEL_TYPE_MAX && cnt < offload_tnl_type_num;
	     i++) {
		tnl_type = mtk_npu_tnl_type_get_by_idx(i);
		if (IS_ERR(tnl_type))
			continue;

		cnt++;
		if (tnl_type->tnl_decap_offloadable
		    && tnl_type->tnl_decap_offloadable(skb)) {
			skb_hnat_tops(skb) = tnl_type->tnl_proto_type;
			inc_tnl_hnat_decap_offloadable_statistic_success();
			return true;
		}
	}

	inc_tnl_hnat_decap_offloadable_statistic_fail();

	return false;
}

static int mtk_npu_tnl_decap_offload(struct sk_buff *skb)
{
	struct npu_tnl_params tnl_params;
	struct npu_tnl_input tnl_input;
	struct npu_tnl_type *tnl_type;
	int ret;

	if (unlikely(!mtk_npu_mcu_alive())) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_decap_offload_statistic_fail();
		return -EAGAIN;
	}

	if (unlikely(!skb_npu_valid(skb) || !skb_hnat_is_decap(skb))) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_decap_offload_statistic_fail();
		return -EINVAL;
	}

	tnl_type = skb_to_tnl_type(skb);
	if (IS_ERR(tnl_type)) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_decap_offload_statistic_fail();
		return PTR_ERR(tnl_type);
	}

	if (unlikely(!tnl_type->tnl_decap_param_setup || !tnl_type->tnl_param_match)) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_decap_offload_statistic_fail();
		return -ENODEV;
	}

	memset(&tnl_params, 0, sizeof(struct npu_tnl_params));
	memset(&tnl_input, 0, sizeof(struct npu_tnl_input));
	tnl_input.hnat.skb = skb;

	/* push removed ethernet header back first */
	if (tnl_type->has_inner_eth)
		skb_push(skb, sizeof(struct ethhdr));

	ret = mtk_npu_decap_param_setup(skb,
					 &tnl_params.params,
					 tnl_type->tnl_decap_param_setup);

	/* pull ethernet header to restore skb->data to ip start */
	if (tnl_type->has_inner_eth)
		skb_pull(skb, sizeof(struct ethhdr));

	/*
	 * We don't need to mark unbind here
	 * Since we may learn the tunnel info in next pre-routing
	 */
	if (unlikely(ret))
		return ret;

	tnl_params.npu_entry_proto = tnl_type->tnl_proto_type;
	tnl_params.cdrt_idx = skb_hnat_cdrt(skb);

	ret = mtk_npu_tnl_offload(&tnl_input,
				  tnl_type,
				  &tnl_params,
				  mtk_npu_nf_hnat_tnl_info_setup);

	/*
	 * whether success or fail to offload a decapsulation tunnel
	 * skb_hnat_tops(skb) must be cleared to avoid mtk_tnl_decap_offload() get
	 * called again
	 */
	skb_hnat_tops(skb) = 0;
	skb_hnat_is_decap(skb) = 0;

	(ret == 0 ? inc_tnl_hnat_decap_offload_statistic_success() :
		    inc_tnl_hnat_decap_offload_statistic_fail());

	return ret;
}

static int mtk_npu_tnl_l2_update(struct sk_buff *skb, struct ethhdr *eth)
{
	struct npu_tnl_info *tnl_info = skb_to_tnl_info(skb);
	struct npu_tnl_type *tnl_type;
	unsigned long flag;
	int ret;

	if (IS_ERR(tnl_info))
		return PTR_ERR(tnl_info);

	tnl_type = tnl_info->tnl_type;
	if (!tnl_type->tnl_l2_param_update)
		return -ENODEV;

	spin_lock_irqsave(&tnl_info->lock, flag);

	ret = tnl_type->tnl_l2_param_update(&tnl_info->cache.params, eth);
	/* tnl params need to be updated */
	if (ret == 1) {
		mtk_npu_tnl_info_submit_no_tnl_lock(tnl_info);
		ret = 0;
	}

	spin_unlock_irqrestore(&tnl_info->lock, flag);

	return ret;
}

static int __mtk_npu_tnl_encap_offload(struct sk_buff *skb, struct ethhdr *eth)
{
	struct npu_tnl_params tnl_params;
	struct npu_tnl_input tnl_input;
	struct npu_tnl_type *tnl_type;
	int ret;

	tnl_type = skb_to_tnl_type(skb);
	if (IS_ERR(tnl_type))
		return PTR_ERR(tnl_type);

	if (unlikely(!tnl_type->tnl_encap_param_setup || !tnl_type->tnl_param_match))
		return -ENODEV;

	memset(&tnl_params, 0, sizeof(struct npu_tnl_params));
	memset(&tnl_input, 0, sizeof(struct npu_tnl_input));

	ret = mtk_npu_encap_param_setup(skb,
					eth,
					&tnl_params.params,
					tnl_type->tnl_encap_param_setup);
	if (unlikely(ret))
		return ret;

	tnl_params.npu_entry_proto = tnl_type->tnl_proto_type;
	tnl_params.cdrt_idx = skb_hnat_cdrt(skb);
	tnl_input.hnat.skb = skb;

	return mtk_npu_tnl_offload(&tnl_input,
				   tnl_type,
				   &tnl_params,
				   mtk_npu_nf_hnat_tnl_info_setup);
}

static int mtk_npu_tnl_encap_offload(struct sk_buff *skb, struct ethhdr *eth)
{
	int ret;

	if (unlikely(!skb || !eth)) {
		inc_tnl_hnat_encap_offload_statistic_fail();
		return -EINVAL;
	}

	if (unlikely(!mtk_npu_mcu_alive())) {
		skb_mark_unbind(skb);
		inc_tnl_hnat_encap_offload_statistic_fail();
		return -EAGAIN;
	}

	if (!skb_hnat_is_encap(skb)) {
		inc_tnl_hnat_encap_offload_statistic_fail();
		return -EPERM;
	}

	if (skb_hnat_cdrt(skb))
		ret = mtk_npu_tnl_l2_update(skb, eth);
	else
		ret = __mtk_npu_tnl_encap_offload(skb, eth);

	(ret == 0 ? inc_tnl_hnat_encap_offload_statistic_success() :
		    inc_tnl_hnat_encap_offload_statistic_fail());

	return ret;
}

static bool mtk_npu_nf_hnat_tnl_is_encrypted_offloadable(struct sk_buff *skb)
{
	return skb && !skb_hnat_cdrt(skb) && skb_dst(skb) && dst_xfrm(skb_dst(skb));
}

static struct net_device *mtk_npu_get_tnl_dev(u8 npu_crsn)
{
	struct npu_tnl_info *tnl_info;

	u32 tnl_idx = npu_crsn;

	if (tnl_idx < NPU_CRSN_TNL_ID_START || tnl_idx > NPU_CRSN_TNL_ID_END)
		return ERR_PTR(-EINVAL);

	tnl_idx = tnl_idx - NPU_CRSN_TNL_ID_START;

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

static struct npu_tnl_offload_ops nf_hnat_tnl_ops = {
	.tnl_is_encrypted_offloadable = mtk_npu_nf_hnat_tnl_is_encrypted_offloadable,
};

int mtk_npu_nf_hnat_tnl_offload_init(void)
{
	int ret;

	ret = mtk_npu_tnl_offload_ops_register(&nf_hnat_tnl_ops);
	if (ret)
		return ret;

	mtk_npu_mcu_state_notifier_register(MCU_STATE_TYPE_NETSTOP, &mcu_netstop_notifier);

	mtk_npu_mcu_state_notifier_register(MCU_STATE_TYPE_INIT, &mcu_init_notifier);

	mtk_npu_gretap_init();

	mtk_npu_l2tpv2_init();

	mtk_npu_pptp_init();

	mtk_npu_vxlan_init();

	mtk_tnl_encap_offload = mtk_npu_tnl_encap_offload;
	mtk_tnl_decap_offload = mtk_npu_tnl_decap_offload;
	mtk_tnl_decap_offloadable = mtk_npu_tnl_decap_offloadable;
	mtk_get_tnl_dev = mtk_npu_get_tnl_dev;

	return 0;
}

void mtk_npu_nf_hnat_tnl_offload_deinit(void)
{
	mtk_npu_gretap_deinit();

	mtk_npu_l2tpv2_deinit();

	mtk_npu_pptp_deinit();

	mtk_npu_vxlan_deinit();

	mtk_tnl_encap_offload = NULL;
	mtk_tnl_decap_offload = NULL;
	mtk_tnl_decap_offloadable = NULL;
	mtk_get_tnl_dev = NULL;

	mtk_npu_tnl_info_flush_all_ppe_tnl();

	mtk_npu_mcu_state_notifier_unregister(MCU_STATE_TYPE_NETSTOP, &mcu_netstop_notifier);

	mtk_npu_mcu_state_notifier_unregister(MCU_STATE_TYPE_INIT, &mcu_init_notifier);
}

void mtk_npu_nf_hnat_statistic_show(struct seq_file *s)
{
	seq_printf(s, "decap offloadable|success: %llu|fail: %llu|\n",
		   hnat_ts.decap_offloadable.success, hnat_ts.decap_offloadable.fail);
	seq_printf(s, "decap offload    |success: %llu|fail: %llu|\n",
		   hnat_ts.decap_offload.success, hnat_ts.decap_offload.fail);
	seq_printf(s, "encap offload    |success: %llu|fail: %llu|\n",
		   hnat_ts.encap_offload.success, hnat_ts.encap_offload.fail);
}

void mtk_npu_nf_hnat_statistic_clear(void)
{
	memset(&hnat_ts, 0, sizeof(struct npu_nf_hnat_statistic));
}

void mtk_npu_nf_hnat_statistic_enable(bool en)
{
	hnat_ts.en = en;
}

bool mtk_npu_nf_hnat_statistic_is_enabled(void)
{
	return mtk_npu_tnl_statistic_is_enabled() && hnat_ts.en;
}
