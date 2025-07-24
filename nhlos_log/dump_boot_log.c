// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define CHAR_WIDTH      8
#define MAX_IO_SIZE     ((CHAR_WIDTH * sizeof(uint32_t) - 1) / 3 + 1)
#define RES_TYPE_IO     0x100
#define RES_TYPE_MEM    0x200

struct res_info {
	struct dentry *debugfs;
	struct dentry *dbg_dir;
	void __iomem *base;
	size_t size;
	u32 restype;
};

static ssize_t logs_read(struct file *flip, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct res_info *log_data = flip->private_data;
	char tmp_buf[MAX_IO_SIZE];
	int res;

	if (!log_data)
		return -EIO;

	if (log_data->restype == RES_TYPE_IO) {
		res = scnprintf(tmp_buf, MAX_IO_SIZE, "%u\n", readl(log_data->base));
		return simple_read_from_buffer(user_buf, count, ppos,
						tmp_buf, res);
	}

	return simple_read_from_buffer(user_buf, count, ppos,
					log_data->base, log_data->size);
}

static const struct file_operations nhlos_logs_debugfs_fops = {
	.owner  = THIS_MODULE,
	.open   = simple_open,
	.read   = logs_read,
	.llseek = default_llseek,
};

static int nhlos_log_probe(struct platform_device *pdev)
{
	struct res_info *log_data;
	struct device_node *node;
	struct resource res;
	int ret = 0;

	log_data = devm_kzalloc(&pdev->dev, sizeof(struct res_info), GFP_KERNEL);
	if (!log_data)
		return -ENOMEM;

	log_data->restype = (u32) of_device_get_match_data(&pdev->dev);
	if (!log_data->restype)
		return -EINVAL;

	node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!node) {
		dev_err(&pdev->dev, "Unable to find memory-region\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret) {
		dev_err(&pdev->dev, "Error getting memory resource:%d\n", ret);
		return ret;
	}

	log_data->size = resource_size(&res);
	if (!log_data->size)
		return -EINVAL;

	if (log_data->restype == RES_TYPE_MEM) {
		log_data->base = devm_memremap(&pdev->dev, res.start,
						log_data->size, MEMREMAP_WB);
	} else {
		log_data->base = devm_ioremap(&pdev->dev, res.start,
						log_data->size);
	}

	if (!log_data->base) {
		dev_err(&pdev->dev, "Failed to remap memory\n");
		return -ENOMEM;
	}

	log_data->dbg_dir = debugfs_lookup("nhlos_logs", NULL);
	if (!log_data->dbg_dir)
		log_data->dbg_dir = debugfs_create_dir("nhlos_logs", NULL);

	log_data->debugfs = debugfs_create_file(pdev->dev.of_node->name, 0444,
						log_data->dbg_dir,
						log_data,
						&nhlos_logs_debugfs_fops);
	if (!log_data->debugfs) {
		dev_err(&pdev->dev, "failed to create debugfs entry\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, log_data);
	return ret;
}

static int nhlos_log_remove(struct platform_device *pdev)
{
	struct res_info *log_data = platform_get_drvdata(pdev);

	debugfs_remove_recursive(log_data->dbg_dir);
	return 0;
}

static const struct of_device_id nhlos_logs_match_table[] = {
	{ .compatible = "qcom,nhlos-logs-io", .data = (void *) RES_TYPE_IO},
	{ .compatible = "qcom,nhlos-logs-mem", .data = (void *) RES_TYPE_MEM},
	{ .compatible = "qcom,nhlos-logs"},
	{}
};
MODULE_DEVICE_TABLE(of, nhlos_logs_match_table);

static struct platform_driver nhlos_log_driver = {
	.probe = nhlos_log_probe,
	.remove = nhlos_log_remove,
	.driver = {
		   .name = "nhlos-log",
		   .of_match_table = nhlos_logs_match_table,
		   },
};

module_platform_driver(nhlos_log_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) NHLOS log driver");
MODULE_LICENSE("GPL");
