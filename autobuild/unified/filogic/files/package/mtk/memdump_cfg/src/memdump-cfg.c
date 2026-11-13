// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 MediaTek Inc. All rights reserved.
 *
 * Helper for configuring memdump
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <asm/byteorder.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/arm-smccc.h>
#include <linux/inet.h>
#include <linux/uaccess.h>

#if KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE
#include <linux/mtk_memdump.h>
#define MDC_HAS_PANIC_CONTROLS
#endif

#define MDC "memdump"
#define MDC_SIP "local_ip"
#define MDC_DIP "dest_ip"
#define MDC_NETMASK "netmask"
#define MDC_EARLY_DUMP "early_dump"
#define MDC_DEFAULT_DUMP "default_dump"

#define MTK_SIP_EMERG_MEM_DUMP_CFG		0xC2000541
#define MDUMP_CFG_LOCAL_IP			0
#define MDUMP_CFG_DEST_IP			1
#define MDUMP_CFG_LOCAL_SUBNET_MASK		2

static struct proc_dir_entry *mdc_entry;
static struct proc_dir_entry *mdc_sip_entry;
static struct proc_dir_entry *mdc_dip_entry;
static struct proc_dir_entry *mdc_netmask_entry;

#ifdef MDC_HAS_PANIC_CONTROLS
struct mdc_dump_control {
	const char *name;
	enum mtk_memdump_dump_position position;
};

static const struct mdc_dump_control mdc_dump_controls[] = {
	{ MDC_EARLY_DUMP, MTK_MEMDUMP_DUMP_EARLY },
	{ MDC_DEFAULT_DUMP, MTK_MEMDUMP_DUMP_DEFAULT },
};

struct mdc_panic_control {
	const char *name;
	enum mtk_memdump_panic_type type;
};

static const struct mdc_panic_control mdc_panic_controls[] = {
	{ "sysrq", MTK_MEMDUMP_PANIC_SYSRQ },
	{ "warn", MTK_MEMDUMP_PANIC_WARN },
	{ "oom", MTK_MEMDUMP_PANIC_OOM },
	{ "hung_task", MTK_MEMDUMP_PANIC_HUNG_TASK },
	{ "soft_lockup", MTK_MEMDUMP_PANIC_SOFT_LOCKUP },
	{ "hard_lockup", MTK_MEMDUMP_PANIC_HARD_LOCKUP },
	{ "rcu_stall", MTK_MEMDUMP_PANIC_RCU_STALL },
};
#endif

static int string_to_ip(const char *s, __be32 *addr)
{
	const char *end;

	if (!in4_pton(s, -1, (u8 *)addr, -1, &end))
		return -EINVAL;

	if (*end && (*end != '\n' || end[1]))
		return -EINVAL;

	return 0;
}

static int user_input_to_ip(const char __user *buffer, size_t count,
			    loff_t *pos, __be32 *addr)
{
	char ipstr[64];

	if (*pos)
		return -EOPNOTSUPP;

	if (count > sizeof(ipstr) - 1)
		return -E2BIG;

	if (copy_from_user(ipstr, buffer, count))
		return -EINVAL;

	ipstr[count] = 0;

	return string_to_ip(ipstr, addr);
}

static int mdc_sip_display(struct seq_file *seq, void *v)
{
	struct arm_smccc_res res = {0};
	uint8_t addr[4];

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 0,
		       MDUMP_CFG_LOCAL_IP, 0, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	memcpy(addr, &res.a1, 4);

	seq_printf(seq, "%u.%u.%u.%u\n", addr[0], addr[1], addr[2], addr[3]);

	return 0;
}

static int mdc_sip_open(struct inode *inode, struct file *file)
{
	return single_open(file, mdc_sip_display, inode->i_private);
}

static ssize_t mdc_sip_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *pos)
{
	struct arm_smccc_res res = {0};
	__be32 addr;
	int ret;

	ret = user_input_to_ip(buffer, count, pos, &addr);
	if (ret)
		return ret;

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 1,
		       MDUMP_CFG_LOCAL_IP, addr, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	return count;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mdc_sip_fops = {
	.proc_open  = mdc_sip_open,
	.proc_read = seq_read,
	.proc_write = mdc_sip_write,
	.proc_lseek  = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations mdc_sip_fops = {
	.open  = mdc_sip_open,
	.read = seq_read,
	.write = mdc_sip_write,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

static int mdc_dip_display(struct seq_file *seq, void *v)
{
	struct arm_smccc_res res = {0};
	uint8_t addr[4];

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 0,
		       MDUMP_CFG_DEST_IP, 0, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	memcpy(addr, &res.a1, 4);

	seq_printf(seq, "%u.%u.%u.%u\n", addr[0], addr[1], addr[2], addr[3]);

	return 0;
}

static int mdc_dip_open(struct inode *inode, struct file *file)
{
	return single_open(file, mdc_dip_display, inode->i_private);
}

static ssize_t mdc_dip_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *pos)
{
	struct arm_smccc_res res = {0};
	__be32 addr;
	int ret;

	ret = user_input_to_ip(buffer, count, pos, &addr);
	if (ret)
		return ret;

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 1,
		       MDUMP_CFG_DEST_IP, addr, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	return count;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mdc_dip_fops = {
	.proc_open  = mdc_dip_open,
	.proc_read = seq_read,
	.proc_write = mdc_dip_write,
	.proc_lseek  = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations mdc_dip_fops = {
	.open  = mdc_dip_open,
	.read = seq_read,
	.write = mdc_dip_write,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

static int mdc_netmask_display(struct seq_file *seq, void *v)
{
	struct arm_smccc_res res = {0};
	uint8_t addr[4];

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 0,
		       MDUMP_CFG_LOCAL_SUBNET_MASK, 0, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	memcpy(addr, &res.a1, 4);

	seq_printf(seq, "%u.%u.%u.%u\n", addr[0], addr[1], addr[2], addr[3]);

	return 0;
}

static int mdc_netmask_open(struct inode *inode, struct file *file)
{
	return single_open(file, mdc_netmask_display, inode->i_private);
}

static ssize_t mdc_netmask_write(struct file *file,
				 const char __user *buffer, size_t count,
				 loff_t *pos)
{
	struct arm_smccc_res res = {0};
	__be32 addr;
	int ret;

	ret = user_input_to_ip(buffer, count, pos, &addr);
	if (ret)
		return ret;

	arm_smccc_smc(MTK_SIP_EMERG_MEM_DUMP_CFG, 1,
		       MDUMP_CFG_LOCAL_SUBNET_MASK, addr, 0, 0, 0, 0, &res);

	if (res.a0)
		return -EINVAL;

	return count;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mdc_netmask_fops = {
	.proc_open  = mdc_netmask_open,
	.proc_read = seq_read,
	.proc_write = mdc_netmask_write,
	.proc_lseek  = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations mdc_netmask_fops = {
	.open  = mdc_netmask_open,
	.read = seq_read,
	.write = mdc_netmask_write,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

#ifdef MDC_HAS_PANIC_CONTROLS
static int mdc_dump_display(struct seq_file *seq, void *v)
{
	enum mtk_memdump_dump_position position = (uintptr_t)seq->private;

	seq_printf(seq, "%d\n", mtk_memdump_get_dump_enabled(position));
	return 0;
}

static int mdc_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, mdc_dump_display, pde_data(inode));
}

static ssize_t mdc_dump_write(struct file *file,
			      const char __user *buffer, size_t count,
			      loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	enum mtk_memdump_dump_position position = (uintptr_t)seq->private;
	char input[2];
	bool enabled;
	int ret;

	if (*pos)
		return -EOPNOTSUPP;

	if (!count || count > sizeof(input))
		return -EINVAL;

	if (copy_from_user(input, buffer, count))
		return -EFAULT;

	if (input[0] != '0' && input[0] != '1')
		return -EINVAL;

	if (count == sizeof(input) && input[1] != '\n')
		return -EINVAL;

	enabled = input[0] == '1';
	ret = mtk_memdump_set_dump_enabled(position, enabled);
	if (ret)
		return ret;

	return count;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mdc_dump_fops = {
	.proc_open = mdc_dump_open,
	.proc_read = seq_read,
	.proc_write = mdc_dump_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations mdc_dump_fops = {
	.open = mdc_dump_open,
	.read = seq_read,
	.write = mdc_dump_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int mdc_create_dump_controls(void)
{
	const struct mdc_dump_control *control;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mdc_dump_controls); i++) {
		control = &mdc_dump_controls[i];
		if (!proc_create_data(control->name, 0644, mdc_entry,
				      &mdc_dump_fops,
				      (void *)(uintptr_t)control->position))
			return -ENOMEM;
	}

	return 0;
}

static int mdc_panic_display(struct seq_file *seq, void *v)
{
	enum mtk_memdump_panic_type type = (uintptr_t)seq->private;

	seq_printf(seq, "%d\n", mtk_memdump_get_panic_enabled(type));
	return 0;
}

static int mdc_panic_open(struct inode *inode, struct file *file)
{
	return single_open(file, mdc_panic_display, pde_data(inode));
}

static ssize_t mdc_panic_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	enum mtk_memdump_panic_type type = (uintptr_t)seq->private;
	char input[2];
	bool enabled;
	int ret;

	if (*pos)
		return -EOPNOTSUPP;

	if (!count || count > sizeof(input))
		return -EINVAL;

	if (copy_from_user(input, buffer, count))
		return -EFAULT;

	if (input[0] != '0' && input[0] != '1')
		return -EINVAL;

	if (count == sizeof(input) && input[1] != '\n')
		return -EINVAL;

	enabled = input[0] == '1';
	ret = mtk_memdump_set_panic_enabled(type, enabled);
	if (ret)
		return ret;

	return count;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mdc_panic_fops = {
	.proc_open = mdc_panic_open,
	.proc_read = seq_read,
	.proc_write = mdc_panic_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations mdc_panic_fops = {
	.open = mdc_panic_open,
	.read = seq_read,
	.write = mdc_panic_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int mdc_create_panic_controls(void)
{
	const struct mdc_panic_control *control;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mdc_panic_controls); i++) {
		control = &mdc_panic_controls[i];
		if (!proc_create_data(control->name, 0644, mdc_entry,
				      &mdc_panic_fops,
				      (void *)(uintptr_t)control->type))
			return -ENOMEM;
	}

	return 0;
}
#endif

static int __init mdc_init(void)
{
#ifdef MDC_HAS_PANIC_CONTROLS
	int ret;
#endif

	mdc_entry = proc_mkdir(MDC, NULL);
	if (!mdc_entry) {
		pr_err("failed to create proc entry " MDC);
		return -1;
	}

	mdc_sip_entry = proc_create(MDC_SIP, 0644, mdc_entry, &mdc_sip_fops);
	if (!mdc_sip_entry) {
		pr_err("failed to create proc entry " MDC "/" MDC_SIP);
		proc_remove(mdc_entry);
		return -1;
	}

	mdc_dip_entry = proc_create(MDC_DIP, 0644, mdc_entry, &mdc_dip_fops);
	if (!mdc_dip_entry) {
		pr_err("failed to create proc entry " MDC "/" MDC_DIP);
		proc_remove(mdc_entry);
		return -1;
	}

	mdc_netmask_entry = proc_create(MDC_NETMASK, 0644, mdc_entry,
					&mdc_netmask_fops);
	if (!mdc_netmask_entry) {
		pr_err("failed to create proc entry " MDC "/" MDC_NETMASK);
		proc_remove(mdc_entry);
		return -1;
	}

#ifdef MDC_HAS_PANIC_CONTROLS
	ret = mdc_create_dump_controls();
	if (ret) {
		pr_err("failed to create memdump position control entries");
		proc_remove(mdc_entry);
		return ret;
	}

	ret = mdc_create_panic_controls();
	if (ret) {
		pr_err("failed to create memdump panic control entries");
		proc_remove(mdc_entry);
		return ret;
	}
#endif

	return 0;
}

static void __exit mdc_exit(void)
{
	proc_remove(mdc_entry);
}

module_init(mdc_init);
module_exit(mdc_exit);

MODULE_AUTHOR("Weijie Gao <weijie.gao@mediatek.com>");
MODULE_DESCRIPTION("Helper for configuring memdump");
MODULE_LICENSE("GPL");
