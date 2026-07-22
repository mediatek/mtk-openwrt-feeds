/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Landen Chao <landen.chao@mediatek.com>
 */

#include <linux/of_device.h>
#include <net/netfilter/nf_flow_table.h>

#include "hnat.h"

int hnat_dsa_get_port(struct net_device **dev)
{
#if defined(CONFIG_NET_DSA)
	struct dsa_port *dp;

	dp = dsa_port_from_netdev(*dev);
	if (IS_ERR(dp))
		return -ENODEV;

	*dev = dp->cpu_dp->conduit;

	return dp->index;
#else
	return -ENODEV;
#endif
}

int hnat_dsa_fill_stag(const struct net_device *netdev,
		       struct foe_entry *entry,
		       u16 eth_proto,
		       int mape)
{
#if defined(CONFIG_NET_DSA)
	const unsigned int *port_reg;
	u16 *sp_tag, *vlan1, *vlan2;
	const struct dsa_port *dp;
	struct net_device *ndev;
	int port_index;
	u16 dsa_tag;

	ndev = (struct net_device *)netdev;

	port_reg = of_get_property(ndev->dev.of_node, "reg", NULL);
	if (unlikely(!port_reg))
		return -EINVAL;

	port_index = be32_to_cpup(port_reg);

	/* In the case MAPE LAN --> WAN, binding entry is to CPU.
	 * Do not add special tag.
	 */
	if (IS_WAN(ndev) && mape)
		return port_index;

	dp = dsa_port_from_netdev(ndev);
	if (IS_ERR(dp))
		return -ENODEV;

	entry->bfib1.vpm = 0;

	if (hnat_entry_vlan_fields(entry, &vlan1, &vlan2, &sp_tag))
		return -EINVAL;

	if (IS_DSA_TAG_PROTO_8021Q(dp)) {
		dsa_tag = port_index | GENMASK(11, 10);

		if (entry->bfib1.vlan_layer) {
			/* PPE entry can only be filled up to 2 VLAN layers,
			 * outer VLAN(vlan1) is preserved for stag.
			 */
			if (unlikely(entry->bfib1.vlan_layer >= 2))
				return -EINVAL;

			/* The inner VLAN should be 802.1Q VLAN tag */
			if (*sp_tag != ETH_P_8021Q)
				return -EINVAL;
			/* Move to inner VLAN if it's already set */
			*vlan2 = *vlan1;
		}
		*vlan1 = dsa_tag;
		*sp_tag = ETH_P_8021Q;

		entry->bfib1.vlan_layer += 1;
	} else if (IS_DSA_TAG_PROTO_MTK(dp)) {
		dsa_tag = BIT(port_index);

		if (!entry->bfib1.vlan_layer) {
			entry->bfib1.vlan_layer = 1;
		} else {
			/* Share the outer layer: encode its TPID (in sp_tag)
			 * into the special tag, as mtk_tag_xmit() does.
			 */
			switch (*sp_tag) {
			case ETH_P_8021Q:
				dsa_tag |= HNAT_STAG_TPID_8021Q;
				break;
			case ETH_P_8021AD:
				dsa_tag |= HNAT_STAG_TPID_8021AD;
				break;
			default:
				return -EINVAL;
			}
		}

		*sp_tag = dsa_tag;

		/* vpm is changed to is_sp_tag after IS_SP_TAG_EN enabled */
		if (hnat_priv->data->version == MTK_HNAT_V3)
			entry->bfib1.vpm = 1;
	} else {
		return -EINVAL;
	}

	return port_index;
#else
	return -EINVAL;
#endif
}
