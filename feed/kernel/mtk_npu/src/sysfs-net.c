// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 MediaTek Inc. All Rights Reserved.
 *
 * Author: Ren-Ting Wang <ren-ting.wang@mediatek.com>
 */

#include <linux/device.h>
#include <linux/slab.h>

#include "npu/adpt-fwd.h"
#include "npu/internal.h"
#include "npu/net-core.h"
#include "npu/rate-limit.h"
#include "npu/sysfs.h"
#include "npu/sysfs-net.h"

#include "npu/protocol/network/l4s.h"

static struct kset *net_kset;

#if defined(CONFIG_MTK_NPU_ADPT_FWD)
static ssize_t adpt_fwd_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_GET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_GET_EN,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "adapter forward enable: %u\n", cmd.ret[0]);
}

static ssize_t adpt_fwd_enable_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_SET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_SET_EN,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return count;

	cmd.arg[1] = val ? 1 : 0;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t adpt_fwd_ul_qid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_GET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_GET_UL_QID,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "adapter forward uplink QID: %u\n", cmd.ret[0]);
}

static ssize_t adpt_fwd_ul_qid_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_SET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_SET_UL_QID,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return count;
	cmd.arg[1] = val;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t adpt_fwd_dl_port_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_GET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_GET_DL_PORT,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "adapter forward downlink switch port: %u\n", cmd.ret[0]);
}

static ssize_t adpt_fwd_dl_port_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_ADPT_FWD,
		.sub_type = NPU_ADPT_FWD_NET_CMD_SET,
		.arg[0] = NPU_ADPT_FWD_NET_CMD_SET_DL_PORT,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return count;
	cmd.arg[1] = val;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static NPU_DEV_ATTR_RW(adpt_fwd, enable);
static NPU_DEV_ATTR_RW(adpt_fwd, ul_qid);
static NPU_DEV_ATTR_RW(adpt_fwd, dl_port);

static struct attribute *adpt_fwd_attributes[] = {
	&dev_attr_adpt_fwd_enable.attr,
	&dev_attr_adpt_fwd_ul_qid.attr,
	&dev_attr_adpt_fwd_dl_port.attr,
	NULL,
};

static const struct attribute_group adpt_fwd_attr_group = {
	.name = "adpt_fwd",
	.attrs = adpt_fwd_attributes,
};

static int adpt_fwd_sysfs_init(void)
{
	int ret;

	ret = sysfs_create_group(&net_kset->kobj, &adpt_fwd_attr_group);
	if (ret)
		NPU_NOTICE("create npu/net/adpt sysfs failed\n");

	return ret;
}

static void adpt_fwd_sysfs_deinit(void)
{
	sysfs_remove_group(&net_kset->kobj, &adpt_fwd_attr_group);
}
#else /* !defined(CONFIG_MTK_NPU_ADPT_FWD) */
static int adpt_fwd_sysfs_init(void)
{
	return 0;
}

static void adpt_fwd_sysfs_deinit(void)
{
}
#endif /* defined(CONFIG_MTK_NPU_ADPT_FWD) */

#if defined(CONFIG_MTK_NPU_L4S)
static ssize_t l4s_alpha_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_GET,
		.arg[0] = NPU_L4S_NET_CMD_GET_ALPHA,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "L4S EWMA alpha: %u\n", cmd.ret[0]);
}

static ssize_t l4s_alpha_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_SET,
		.arg[0] = NPU_L4S_NET_CMD_SET_ALPHA,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > 100) {
		pr_notice("Error: You can only set alpha to 0 ~ 100\n");
		return -EIO;
	}
	cmd.arg[1] = val;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t l4s_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_GET,
		.arg[0] = NPU_L4S_NET_CMD_GET_EN,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "L4S enable: %u\n", cmd.ret[0]);
}

static ssize_t l4s_enable_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_SET,
		.arg[0] = NPU_L4S_NET_CMD_SET_EN,
	};
	u8 en;

	if (kstrtou8(buf, 10, &en))
		return -EINVAL;

	cmd.arg[1] = en ? 1 : 0;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t l4s_interval_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_GET,
		.arg[0] = NPU_L4S_NET_CMD_GET_INTERVAL,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "L4S sampling intervals: %u\n", cmd.ret[0]);
}

static ssize_t l4s_interval_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_SET,
		.arg[0] = NPU_L4S_NET_CMD_SET_INTERVAL,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > 5000) {
		pr_notice("Error: You can only set intervals to 0 ~ 5000\n");
		return -EIO;
	}
	cmd.arg[1] = val;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t l4s_qos_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_GET,
	};
	u32 dl_qid_default;
	u32 dl_qid_l4s_capable;
	u32 ul_qid_default;
	u32 ul_qid_l4s_capable;
	int ret;

	cmd.arg[0] = NPU_L4S_NET_CMD_GET_DL_DEFAULT_QID;
	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;
	dl_qid_default = cmd.ret[0];

	cmd.arg[0] = NPU_L4S_NET_CMD_GET_DL_CAPABLE_QID;
	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;
	dl_qid_l4s_capable = cmd.ret[0];

	cmd.arg[0] = NPU_L4S_NET_CMD_GET_UL_DEFAULT_QID;
	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;
	ul_qid_default = cmd.ret[0];

	cmd.arg[0] = NPU_L4S_NET_CMD_GET_UL_CAPABLE_QID;
	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;
	ul_qid_l4s_capable = cmd.ret[0];

	return scnprintf(buf, PAGE_SIZE,
			 "L4S downlink qid default: %u, qid l4s_capable: %u\n"
			 "L4S uplink qid default: %u, qid l4s_capable: %u\n",
			 dl_qid_default, dl_qid_l4s_capable, ul_qid_default, ul_qid_l4s_capable);
}

static ssize_t l4s_qos_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_SET,
	};
	char dir[20];
	char q_type[20];
	int ret;
	u32 qid;

	ret = sscanf(buf, "%20s %20s %u", dir, q_type, &qid);
	if (ret != 3)
		return -EINVAL;

	if (!strncmp(dir, "downlink", strlen("downlink"))) {
		if (!strncmp(q_type, "default", strlen("default")))
			cmd.arg[0] = NPU_L4S_NET_CMD_SET_DL_DEFAULT_QID;
		else if (!strncmp(q_type, "l4s_capable", strlen("l4s_capable")))
			cmd.arg[0] = NPU_L4S_NET_CMD_SET_DL_CAPABLE_QID;
		else
			return -EINVAL;
	} else if (!strncmp(dir, "uplink", strlen("uplink"))) {
		if (!strncmp(q_type, "default", strlen("default")))
			cmd.arg[0] = NPU_L4S_NET_CMD_SET_UL_DEFAULT_QID;
		else if (!strncmp(q_type, "l4s_capable", strlen("l4s_capable")))
			cmd.arg[0] = NPU_L4S_NET_CMD_SET_UL_CAPABLE_QID;
		else
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	/* TODO: validate qid range */
	cmd.arg[1] = qid;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t l4s_threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_GET,
		.arg[0] = NPU_L4S_NET_CMD_GET_THRESHOLD,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt == 0)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "L4S mark congestion threshold: %u\n", cmd.ret[0]);
}

static ssize_t l4s_threshold_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_L4S,
		.sub_type = NPU_L4S_NET_CMD_SET,
		.arg[0] = NPU_L4S_NET_CMD_SET_THRESHOLD,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > 100) {
		pr_notice("Error: You can only set threshold to 0 ~ 100\n");
		return -EIO;
	}
	cmd.arg[1] = val;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static NPU_DEV_ATTR_RW(l4s, alpha);
static NPU_DEV_ATTR_RW(l4s, enable);
static NPU_DEV_ATTR_RW(l4s, interval);
static NPU_DEV_ATTR_RW(l4s, qos);
static NPU_DEV_ATTR_RW(l4s, threshold);

static struct attribute *l4s_attributes[] = {
	&dev_attr_l4s_alpha.attr,
	&dev_attr_l4s_enable.attr,
	&dev_attr_l4s_interval.attr,
	&dev_attr_l4s_threshold.attr,
	&dev_attr_l4s_qos.attr,
	NULL,
};

static const struct attribute_group l4s_attr_group = {
	.name = "l4s",
	.attrs = l4s_attributes,
};

static int l4s_sysfs_init(void)
{
	int ret;

	ret = sysfs_create_group(&net_kset->kobj, &l4s_attr_group);
	if (ret)
		NPU_NOTICE("create npu/net/l4s sysfs failed\n");

	return ret;
}

static void l4s_sysfs_deinit(void)
{
}
#else /* !defined(CONFIG_MTK_NPU_L4S) */
static int l4s_sysfs_init(void)
{
	return 0;
}

static void l4s_sysfs_deinit(void)
{
}
#endif /* defined(CONFIG_MTK_NPU_L4S) */

#if defined(CONFIG_MTK_NPU_RATE_LIMIT)
static ssize_t rate_limit_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_RATE_LIMIT,
		.sub_type = NPU_RATE_LIMIT_NET_CMD_GET_CFG,
		.arg[0] = NPU_RATE_LIMIT_NET_CMD_GET_EN,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt != 1)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "rate limit enable: %u\n", cmd.ret[0]);
}

static ssize_t rate_limit_enable_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_RATE_LIMIT,
		.sub_type = NPU_RATE_LIMIT_NET_CMD_SET_CFG,
		.arg[0] = NPU_RATE_LIMIT_NET_CMD_SET_EN,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	cmd.arg[1] = val ? 1 : 0;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static ssize_t rate_limit_queue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	static const char *q_name[RATE_LIMIT_QUEUE_NUM] = {
		"low",
		"mid",
		"high",
		"privileged",
	};
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_RATE_LIMIT,
		.sub_type = NPU_RATE_LIMIT_NET_CMD_GET_QUEUE,
	};
	u32 len = 0;
	int ret;
	int i;

	cmd.sub_type = NPU_RATE_LIMIT_NET_CMD_GET_QUEUE;
	for (i = 0; i < RATE_LIMIT_QUEUE_NUM; i++) {
		cmd.arg[0] = i;
		ret = mtk_npu_net_send_cmd_mgmt(&cmd);
		if (ret || cmd.return_cnt != 1)
			return 0;

		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "%10s queue ID: %u\n",
				 q_name[i],
				 cmd.ret[0]);
	}

	return len;
}

static ssize_t rate_limit_queue_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_RATE_LIMIT,
		.sub_type = NPU_RATE_LIMIT_NET_CMD_SET_QUEUE,
	};
	int qtype;
	int qid;
	int ret;

	ret = sscanf(buf, "%u %u", &qtype, &qid);
	if (ret != 2)
		return -EINVAL;
	cmd.arg[0] = qtype;
	cmd.arg[1] = qid;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static NPU_DEV_ATTR_RW(rate_limit, enable);
static NPU_DEV_ATTR_RW(rate_limit, queue);

static struct attribute *rate_limit_attributes[] = {
	&dev_attr_rate_limit_enable.attr,
	&dev_attr_rate_limit_queue.attr,
	NULL,
};

static const struct attribute_group rate_limit_attr_group = {
	.name = "rate_limit",
	.attrs = rate_limit_attributes,
};

static int rate_limit_sysfs_init(void)
{
	int ret;

	ret = sysfs_create_group(&net_kset->kobj, &rate_limit_attr_group);
	if (ret)
		NPU_NOTICE("create npu/net/rate_limit sysfs failed\n");

	return ret;
}

static void rate_limit_sysfs_deinit(void)
{
}
#else /* !defined(CONFIG_MTK_NPU_RATE_LIMIT) */
static int rate_limit_sysfs_init(void)
{
	return 0;
}

static void rate_limit_sysfs_deinit(void)
{
}
#endif /* defined(CONFIG_MTK_NPU_RATE_LIMIT) */

static ssize_t ip_reasm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_CORE,
		.sub_type = NPU_NET_CORE_CMD_IP_REASM_EN_GET,
	};
	int ret;

	ret = mtk_npu_net_send_cmd_mgmt(&cmd);
	if (ret || cmd.return_cnt != 1)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "NPU IP reassembly enable: %u\n", cmd.ret[0]);
}

static ssize_t ip_reasm_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct net_cmd cmd = {
		.type = NPU_NET_CMD_TYPE_CORE,
		.sub_type = NPU_NET_CORE_CMD_IP_REASM_EN_SET,
	};
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return count;

	cmd.arg[0] = val ? 1 : 0;

	mtk_npu_net_send_cmd_all_no_wait(&cmd);

	return count;
}

static DEVICE_ATTR_RW(ip_reasm);

int mtk_npu_sysfs_net_init(struct platform_device *pdev)
{
	int ret;

	net_kset = kzalloc(sizeof(struct kset), GFP_KERNEL);
	if (!net_kset) {
		NPU_NOTICE("allocate npu/net/ kset failed\n");
		return -ENOMEM;
	}

	net_kset->kobj.kset = npu_kset;
	net_kset->kobj.ktype = npu_kset->kobj.ktype;
	net_kset->kobj.parent = &npu_kset->kobj;
	ret = kobject_set_name(&net_kset->kobj, "net");
	if (ret) {
		kfree(net_kset);
		return ret;
	}

	ret = kset_register(net_kset);
	if (ret) {
		NPU_NOTICE("initialize npu/net/ kset failed: %d\n", ret);
		kfree(net_kset);
		return ret;
	}

	ret = adpt_fwd_sysfs_init();
	if (ret) {
		mtk_npu_sysfs_net_deinit(pdev);
		return ret;
	}

	ret = l4s_sysfs_init();
	if (ret) {
		mtk_npu_sysfs_net_deinit(pdev);
		return ret;
	}

	ret = rate_limit_sysfs_init();
	if (ret) {
		mtk_npu_sysfs_net_deinit(pdev);
		return ret;
	}

	ret = sysfs_create_file(&net_kset->kobj, &dev_attr_ip_reasm.attr);
	if (ret) {
		NPU_NOTICE("create npu/net/ip_reasm sysfs failed\n");
		mtk_npu_sysfs_net_deinit(pdev);
		return ret;
	}

	return ret;
}

void mtk_npu_sysfs_net_deinit(struct platform_device *pdev)
{
	sysfs_remove_file(&net_kset->kobj, &dev_attr_ip_reasm.attr);

	rate_limit_sysfs_deinit();
	l4s_sysfs_deinit();
	adpt_fwd_sysfs_deinit();

	kset_unregister(net_kset);
}
