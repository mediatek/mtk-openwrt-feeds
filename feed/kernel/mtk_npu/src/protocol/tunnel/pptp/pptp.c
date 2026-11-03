// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Frank-zj Lin <frank-zj.lin@mediatek.com>
 */

#include <linux/if_pppox.h>
#include <linux/ppp_channel.h>

#include <net/gre.h>
#include <net/pptp.h>
#include <net/sock.h>

#include <pce/cls.h>
#include <pce/netsys.h>
#include <pce/pce.h>

#include "npu/netsys.h"
#include "npu/protocol/mac/ppp.h"
#include "npu/protocol/tunnel/pptp/pptp.h"
#include "npu/seq_gen.h"
#include "npu/tunnel.h"

struct pptp_encap_statistic {
	u64 offload_invalid;
	u64 null_hdr_ptr;
	u64 ppp_invalid;
	u64 seq_idx_get_fail;
	u64 success;
};

struct pptp_decap_statistic {
	u64 ppp_invalid;
	u64 pptp_gre_len_invalid;
	u64 null_hdr_ptr;
	u64 offload_invalid;
	u64 sock_get_fail;
	u64 success;
};

struct pptp_statistic {
	struct npu_tnl_statistic ts;
	struct pptp_encap_statistic encap;
	struct pptp_decap_statistic decap;
};

static struct pptp_statistic pptp_ts;

static inline void inc_pptp_statistic_encap_offload_invalid(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.encap.offload_invalid++;
}

static inline void inc_pptp_statistic_encap_null_hdr_ptr(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.encap.null_hdr_ptr++;
}

static inline void inc_pptp_statistic_encap_ppp_invalid(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.encap.ppp_invalid++;
}

static inline void inc_pptp_statistic_encap_seq_idx_get_fail(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.encap.seq_idx_get_fail++;
}

static inline void inc_pptp_statistic_encap_success(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.encap.success++;
}

static inline void inc_pptp_statistic_decap_ppp_invalid(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.ppp_invalid++;
}

static inline void inc_pptp_statistic_decap_pptp_gre_len_invalid(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.pptp_gre_len_invalid++;
}

static inline void inc_pptp_statistic_decap_null_hdr_ptr(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.null_hdr_ptr++;
}

static inline void inc_pptp_statistic_decap_offload_invalid(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.offload_invalid++;
}

static inline void inc_pptp_statistic_decap_sock_get_fail(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.sock_get_fail++;
}

static inline void inc_pptp_statistic_decap_success(void)
{
	if (!mtk_npu_tnl_statistic_is_enabled())
		return;

	pptp_ts.decap.success++;
}

static int pptp_cls_entry_setup(struct npu_tnl_info *tnl_info,
				struct cls_desc *cdesc)
{
	CLS_DESC_DATA(cdesc, fport, PSE_PORT_TDMA);
	CLS_DESC_MASK_DATA(cdesc, tag, CLS_DESC_TAG_MASK, CLS_DESC_TAG_MATCH_L4_HDR);
	CLS_DESC_MASK_DATA(cdesc, dip_match, CLS_DESC_DIP_MATCH, CLS_DESC_DIP_MATCH);
	CLS_DESC_MASK_DATA(cdesc, l4_type, CLS_DESC_L4_TYPE_MASK, IPPROTO_GRE);
	CLS_DESC_MASK_DATA(cdesc, l4_udp_hdr_nez,
			   CLS_DESC_UDPLITE_L4_HDR_NEZ_MASK,
			   CLS_DESC_UDPLITE_L4_HDR_NEZ_MASK);
	CLS_DESC_MASK_DATA(cdesc, l4_valid,
			   CLS_DESC_L4_VALID_MASK,
			   CLS_DESC_VALID_UPPER_HALF_WORD_BIT |
			   CLS_DESC_VALID_LOWER_HALF_WORD_BIT);
	CLS_DESC_MASK_DATA(cdesc, l4_hdr_usr_data, 0x0000FFFF, 0x0000880B);

	return 0;
}

/*
 * If a sequence generator is already allocated for this tunnel (call_id),
 * return with seq_gen_idx set. Otherwise, allocate a new sequence generator
 * and set the starting sequence number.
 */
static int pptp_get_seq_gen_idx(uint16_t call_id, uint32_t seq_start,
				int *seq_gen_idx)
{
	int ret;

	ret = mtk_npu_pptp_seq_get_seq_gen_idx(call_id, seq_gen_idx);
	if (ret)
		ret = mtk_npu_pptp_seq_alloc(call_id, seq_start, seq_gen_idx);

	return ret;
}

static inline bool pptp_gre_offload_valid(struct sk_buff *skb)
{
	struct pptp_gre_header *pptp_gre;
	struct pptp_gre_header pptp_greh;

	pptp_gre = skb_header_pointer(skb, 0, sizeof(struct pptp_gre_header), &pptp_greh);
	if (unlikely(!pptp_gre))
		return false;

	if (pptp_gre->gre_hd.protocol != GRE_PROTO_PPP
	    || pptp_gre->payload_len < sizeof(struct ppp_hdr)
	    || GRE_IS_CSUM(pptp_gre->gre_hd.flags)	/* flag CSUM should be clear */
	    || GRE_IS_ROUTING(pptp_gre->gre_hd.flags)	/* flag ROUTING should be clear */
	    || !GRE_IS_KEY(pptp_gre->gre_hd.flags)	/* flag KEY should be set */
	    || pptp_gre->gre_hd.flags & GRE_FLAGS)	/* flag Recursion Ctrl should be clear */
		return false;

	return true;
}

static inline int pptp_gre_len_evaluate(struct sk_buff *skb)
{
	static const int possible_greh_len[] = {
		sizeof(struct pptp_gre_header) - PPTP_GRE_HDR_ACK_LEN,
		sizeof(struct pptp_gre_header),
	};
	struct pptp_gre_header *pptp_gre;
	int pptp_gre_len;
	int i;

	for (i = 0; i < ARRAY_SIZE(possible_greh_len); i++) {
		pptp_gre_len = possible_greh_len[i];

		skb_push(skb, pptp_gre_len);
		pptp_gre = (struct pptp_gre_header *)skb->data;
		skb_pull(skb, pptp_gre_len);

		if (pptp_gre->gre_hd.protocol == GRE_PROTO_PPP)
			return pptp_gre_len;
	}

	return -EINVAL;
}

#if KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE
static void pptp_tnl_flow_param_setup(const struct net_device_path *path,
				      struct npu_params *params)
{
	struct npu_pptp_params *pptpp = &params->tunnel.pptp;
	struct npu_ip_params *ipp = &params->network.ip;
	int seq_gen_idx;

	params->mac.type = NPU_MAC_ETH;
	memcpy(params->mac.eth.h_dest, path->tunnel.h_dest, ETH_ALEN);
	memcpy(params->mac.eth.h_source, path->tunnel.h_source, ETH_ALEN);
	params->mac.eth.h_proto = htons(ETH_P_IP);

	params->network.type = NPU_NETWORK_IP;
	ipp->sip = path->tunnel.sip.ip;
	ipp->dip = path->tunnel.dip.ip;
	ipp->proto = IPPROTO_GRE;

	params->tunnel.type = NPU_TUNNEL_PPTP;
	pptpp->dl_call_id = htons(path->tunnel.pptp.call_id);
	pptpp->ul_call_id = htons(path->tunnel.pptp.peer_call_id);

	if (pptp_get_seq_gen_idx(path->tunnel.pptp.peer_call_id,
				 path->tunnel.pptp.seq_sent, &seq_gen_idx))
		return;
	pptpp->seq_gen_idx = (u8)seq_gen_idx;
}
#endif

static int pptp_tnl_decap_param_setup(struct sk_buff *skb,
				      struct npu_params *params)
{
	struct pptp_gre_header *pptp_gre;
	struct pptp_gre_header pptp_greh;
	struct npu_pptp_params *pptpp;
	struct sock *sk;
	int pptp_gre_len;
	int ret = 0;

	/* ppp */
	skb_push(skb, sizeof(struct ppp_hdr));
	if (unlikely(!mtk_npu_ppp_valid(skb))) {
		ret = -EINVAL;
		inc_pptp_statistic_decap_ppp_invalid();
		goto restore_ppp;
	}

	/* pptp_gre */
	pptp_gre_len = pptp_gre_len_evaluate(skb);
	if (pptp_gre_len < 0) {
		ret = -EINVAL;
		inc_pptp_statistic_decap_pptp_gre_len_invalid();
		goto restore_ppp;
	}

	skb_push(skb, pptp_gre_len);
	pptp_gre = skb_header_pointer(skb, 0, pptp_gre_len, &pptp_greh);
	if (unlikely(!pptp_gre)) {
		ret = -EINVAL;
		inc_pptp_statistic_decap_null_hdr_ptr();
		goto restore_pptp_gre;
	}

	if (unlikely(!pptp_gre_offload_valid(skb))) {
		ret = -EINVAL;
		inc_pptp_statistic_decap_offload_invalid();
		goto restore_pptp_gre;
	}

	/*
	 * In decap setup, dl_call_id is fetched from the skb and ul_call_id is
	 * fetched from socket struct of ppp device.
	 */
	sk = ppp_netdev_get_sock(skb->dev);
	if (IS_ERR(sk)) {
		ret = PTR_ERR(sk);
		inc_pptp_statistic_decap_sock_get_fail();
		goto restore_pptp_gre;
	}

	params->tunnel.type = NPU_TUNNEL_PPTP;
	pptpp = &params->tunnel.pptp;
	pptpp->dl_call_id = pptp_gre->call_id;
	pptpp->ul_call_id = htons(pppox_sk(sk)->proto.pptp.dst_addr.call_id);

	ret = mtk_npu_network_decap_param_setup(skb, params);

restore_pptp_gre:
	skb_pull(skb, pptp_gre_len);

restore_ppp:
	skb_pull(skb, sizeof(struct ppp_hdr));

	if (!ret)
		inc_pptp_statistic_decap_success();

	return ret;
}

static int pptp_tnl_encap_param_setup(struct sk_buff *skb,
				      struct npu_params *params)
{
	struct pptp_gre_header *pptp_gre;
	struct pptp_gre_header pptp_greh;
	struct npu_pptp_params *pptpp;
	uint32_t pptp_gre_len;
	int seq_gen_idx;
	int ret = 0;

	if (unlikely(!pptp_gre_offload_valid(skb))) {
		inc_pptp_statistic_encap_offload_invalid();
		return -EINVAL;
	}

	pptp_gre = skb_header_pointer(skb, 0, sizeof(struct pptp_gre_header), &pptp_greh);
	if (unlikely(!pptp_gre)) {
		inc_pptp_statistic_encap_null_hdr_ptr();
		return -EINVAL;
	}

	pptp_gre_len = sizeof(*pptp_gre);
	if (!(GRE_IS_ACK(pptp_gre->gre_hd.flags)))
		pptp_gre_len -= sizeof(pptp_gre->ack);

	skb_pull(skb, pptp_gre_len);

	/* check ppp */
	if (unlikely(!mtk_npu_ppp_valid(skb))) {
		ret = -EINVAL;
		inc_pptp_statistic_encap_ppp_invalid();
		goto restore_pptp_gre;
	}

	ret = pptp_get_seq_gen_idx(ntohs(pptp_gre->call_id),
				   ntohl(pptp_gre->seq), &seq_gen_idx);
	if (ret) {
		inc_pptp_statistic_encap_seq_idx_get_fail();
		goto restore_pptp_gre;
	}

	params->tunnel.type = NPU_TUNNEL_PPTP;
	pptpp = &params->tunnel.pptp;
	pptpp->seq_gen_idx = (u8)seq_gen_idx;
	pptpp->ul_call_id = pptp_gre->call_id;

restore_pptp_gre:
	skb_push(skb, pptp_gre_len);

	if (!ret)
		inc_pptp_statistic_encap_success();

	return ret;
}

static int pptp_debug_param_fetch_call_id(const char *buf, int *ofs, u16 *call_id)
{
	int nchar = 0;
	int ret;
	u16 c = 0;

	ret = sscanf(buf + *ofs, "%hu %n", &c, &nchar);
	if (ret != 1)
		return -EPERM;

	*call_id = htons(c);

	*ofs += nchar;

	return 0;
}

static int pptp_tnl_debug_param_setup(const char *buf, int *ofs,
				      struct npu_params *params)
{
	struct npu_pptp_params *pptpp;
	int seq_gen_idx;
	int ret;

	pptpp = &params->tunnel.pptp;

	ret = pptp_debug_param_fetch_call_id(buf, ofs, &pptpp->ul_call_id);
	if (ret)
		return ret;

	ret = pptp_debug_param_fetch_call_id(buf, ofs, &pptpp->dl_call_id);
	if (ret)
		return ret;

	ret = pptp_get_seq_gen_idx(ntohs(pptpp->ul_call_id), 0, &seq_gen_idx);
	if (ret)
		return ret;

	pptpp->seq_gen_idx = (u8)seq_gen_idx;

	return 0;
}

static bool pptp_tnl_decap_offloadable(struct sk_buff *skb)
{
	struct pptp_gre_header *pptp_gre;
	struct pptp_gre_header pptp_greh;
	struct iphdr *ip;
	int pptp_gre_len;
	int ip_len;
	bool ret = true;

	/* check ip */
	ip = ip_hdr(skb);
	if (ip->protocol != IPPROTO_GRE)
		return false;

	ip_len = ip_hdr(skb)->ihl * 4;

	skb_pull(skb, ip_len);

	/* check gre */
	if (!pptp_gre_offload_valid(skb)) {
		ret = false;
		goto restore_ip;
	}

	pptp_gre = skb_header_pointer(skb, 0, sizeof(struct pptp_gre_header), &pptp_greh);
	if (unlikely(!pptp_gre)) {
		ret = false;
		goto restore_ip;
	}

	pptp_gre_len = sizeof(*pptp_gre);
	if (!(GRE_IS_ACK(pptp_gre->gre_hd.flags)))
		pptp_gre_len -= sizeof(pptp_gre->ack);

	skb_pull(skb, pptp_gre_len);

	/* check ppp */
	if (unlikely(!mtk_npu_ppp_valid(skb))) {
		ret = false;
		goto restore_pptp_gre;
	}

restore_pptp_gre:
	skb_push(skb, pptp_gre_len);

restore_ip:
	skb_push(skb, ip_len);

	return ret;
}

static void pptp_tnl_param_restore(struct npu_params *old, struct npu_params *new)
{
	/* dl_call_id is assigned at decap */
	if (old->tunnel.pptp.dl_call_id)
		new->tunnel.pptp.dl_call_id = old->tunnel.pptp.dl_call_id;

	if (old->tunnel.pptp.ul_call_id)
		new->tunnel.pptp.ul_call_id = old->tunnel.pptp.ul_call_id;

	/* seq_gen_idx is assigned at encap */
	if (old->tunnel.pptp.seq_gen_idx)
		new->tunnel.pptp.seq_gen_idx = old->tunnel.pptp.seq_gen_idx;
}

static bool pptp_tnl_param_match(struct npu_params *p, struct npu_params *target)
{
	/*
	 * Only ul_call_id is guaranteed to be valid for comparison, dl_call_id
	 * may be left empty if no DL traffic had passed yet.
	 */
	return p->tunnel.pptp.ul_call_id == target->tunnel.pptp.ul_call_id;
}

static void pptp_tnl_param_dump(struct seq_file *s, struct npu_params *params)
{
	struct npu_pptp_params *pptpp = &params->tunnel.pptp;

	seq_puts(s, "\tTunnel Type: PPTP ");
	seq_printf(s, "DL Call ID: %05u UL Call ID: %05u SEQ_GEN_IDX: %05u\n",
		   ntohs(pptpp->dl_call_id), ntohs(pptpp->ul_call_id),
		   pptpp->seq_gen_idx);
}

static void pptp_tnl_statistic_encap_dump(struct seq_file *s, struct npu_tnl_type *tnl_type)
{
	seq_printf(s, "success: %llu|null header pointer: %llu|offload invalid: %llu|\n"
		      "      |ppp invalid: %llu|sequence id get fail: %llu|\n",
		   pptp_ts.encap.success,
		   pptp_ts.encap.null_hdr_ptr,
		   pptp_ts.encap.offload_invalid,
		   pptp_ts.encap.ppp_invalid,
		   pptp_ts.encap.seq_idx_get_fail);
}

static void pptp_tnl_statistic_decap_dump(struct seq_file *s, struct npu_tnl_type *tnl_type)
{
	seq_printf(s, "success: %llu|null header pointer: %llu|offload invalid: %llu|\n"
		      "      |ppp invalid: %llu|invalid pptp gre header length: %llu|\n"
		      "      |socket get failed: %llu|\n",
		   pptp_ts.decap.success,
		   pptp_ts.decap.null_hdr_ptr,
		   pptp_ts.decap.offload_invalid,
		   pptp_ts.decap.ppp_invalid,
		   pptp_ts.decap.pptp_gre_len_invalid,
		   pptp_ts.decap.sock_get_fail);
}

static void pptp_tnl_statistic_clear(struct npu_tnl_type *tnl_type)
{
	memset(&pptp_ts, 0, sizeof(struct pptp_statistic));
}

static struct npu_tnl_type pptp_type = {
	.type_name = TYPE_NAME_PPTP,
	.cls_entry_setup = pptp_cls_entry_setup,
#if KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE
	.ndev_path_type = DEV_PATH_PPTP,
	.tnl_flow_param_setup = pptp_tnl_flow_param_setup,
#endif
	.tnl_decap_param_setup = pptp_tnl_decap_param_setup,
	.tnl_encap_param_setup = pptp_tnl_encap_param_setup,
	.tnl_debug_param_setup = pptp_tnl_debug_param_setup,
	.tnl_decap_offloadable = pptp_tnl_decap_offloadable,
	.tnl_param_restore = pptp_tnl_param_restore,
	.tnl_param_match = pptp_tnl_param_match,
	.tnl_param_dump = pptp_tnl_param_dump,
	.tnl_proto_type = NPU_TUNNEL_PPTP,
	.has_inner_eth = false,
	.max_mtu = 1464,
	.ts = &pptp_ts.ts,
	.tnl_statistic_encap_dump = pptp_tnl_statistic_encap_dump,
	.tnl_statistic_decap_dump = pptp_tnl_statistic_decap_dump,
	.tnl_statistic_clear = pptp_tnl_statistic_clear,
};

int mtk_npu_pptp_init(void)
{
	int ret = 0;

	ret = mtk_npu_tnl_type_register(&pptp_type);
	if (ret)
		return ret;

	mtk_npu_pptp_seq_init();

	return ret;
}
EXPORT_SYMBOL(mtk_npu_pptp_init);

void mtk_npu_pptp_deinit(void)
{
	mtk_npu_pptp_seq_deinit();

	mtk_npu_tnl_type_unregister(&pptp_type);
}
EXPORT_SYMBOL(mtk_npu_pptp_deinit);
