// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#define TZ_STR_LEN      12
#define TZ_SIZE         ((CHAR_WIDTH * sizeof(uint32_t) - 1) / 3 + 1)

enum mem_type {
	RES_MEM,
	IO_MEM
};

struct xbl_log_data {
	struct device *dev;
	void *xbl_buf;
	size_t buf_size;
	struct dentry *dbg_file;
	struct debugfs_blob_wrapper dbg_data;
};

static int append_buf_data(struct xbl_log_data *xbl_data, const char *name,
			    void __iomem *buf, size_t sz, enum mem_type mem)
{
	u32 pos;

	if (!xbl_data->xbl_buf)
		xbl_data->xbl_buf = devm_kzalloc(xbl_data->dev,
						 xbl_data->buf_size,
						 GFP_KERNEL);
	else
		xbl_data->xbl_buf = devm_krealloc(xbl_data->dev,
						  xbl_data->xbl_buf,
						  xbl_data->buf_size,
						  GFP_KERNEL);

	if (!xbl_data->xbl_buf) {
		dev_err(xbl_data->dev, "Buffer memory allocation failed\n");
		return -ENOMEM;
	}

	pos = xbl_data->buf_size - sz;

	switch (mem) {
	case RES_MEM:
		memcpy(xbl_data->xbl_buf + pos, buf, sz);
		break;
	case IO_MEM:
		if (!strncmp(name, "tz-start-count", 14))
			scnprintf(xbl_data->xbl_buf + pos, sz,
				  "TZ Start [%u]\n", readl(buf));
		else
			scnprintf(xbl_data->xbl_buf + pos, sz,
				  "TZ End   [%u]\n", readl(buf));
		break;
	};

	return 0;
}

static int map_addr_range(struct device_node **parent, const char *name,
			  struct xbl_log_data *xbl_data, enum mem_type mem)
{
	struct device_node *node;
	void __iomem *tmp_buf;
	struct resource res;
	size_t tmp_sz;
	int ret;

	node = of_find_node_by_name(*parent, name);
	if (!node)
		return -ENODEV;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(xbl_data->dev, "Failed to parse memory region\n");
		return ret;
	}
	of_node_put(node);

	if (!resource_size(&res)) {
		dev_err(xbl_data->dev, "Failed to parse memory region size\n");
		return -ENODEV;
	}

	switch (mem) {
	case RES_MEM:
		tmp_sz = resource_size(&res) - 1;
		tmp_buf = devm_memremap(xbl_data->dev, res.start, tmp_sz,
					MEMREMAP_WB);
		if (!tmp_buf) {
			dev_err(xbl_data->dev, "%s: mem remap failed\n", name);
			return -ENOMEM;
		}
		xbl_data->buf_size += tmp_sz;
		break;
	case IO_MEM:
		tmp_sz = TZ_SIZE;
		tmp_buf =
		    (void *)devm_ioremap(xbl_data->dev, res.start, tmp_sz);
		if (!tmp_buf) {
			dev_err(xbl_data->dev, "%s: io remap failed\n", name);
			return -ENOMEM;
		}
		tmp_sz += TZ_STR_LEN;
		xbl_data->buf_size += tmp_sz;
		break;
	default:
		dev_err(xbl_data->dev, "%s: incorrect memory type\n", name);
		return -ENOMEM;
	}

	return append_buf_data(xbl_data, name, tmp_buf, tmp_sz, mem);
}

static int xbl_log_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xbl_log_data *xbl_data;
	struct device_node *parent;
	int ret;

	xbl_data = devm_kzalloc(dev, sizeof(*xbl_data), GFP_KERNEL);
	if (!xbl_data)
		return -ENOMEM;

	xbl_data->dev = &pdev->dev;
	platform_set_drvdata(pdev, xbl_data);

	parent = of_find_node_by_path("/reserved-memory");
	if (!parent) {
		dev_err(xbl_data->dev, "reserved-memory node missing\n");
		return -ENODEV;
	}

	ret = map_addr_range(&parent, "uefi-log", xbl_data, RES_MEM);
	if (ret)
		goto put_node;

	ret = map_addr_range(&parent, "tz-start-count", xbl_data, IO_MEM);
	if (ret)
		goto put_node;

	ret = map_addr_range(&parent, "tz-end-count", xbl_data, IO_MEM);
	if (ret)
		goto put_node;

	xbl_data->dbg_data.data = xbl_data->xbl_buf;
	xbl_data->dbg_data.size = xbl_data->buf_size;
	xbl_data->dbg_file = debugfs_create_blob("xbl_log", 0400, NULL,
						 &xbl_data->dbg_data);
	if (IS_ERR(xbl_data->dbg_file)) {
		dev_err(xbl_data->dev, "failed to create debugfs entry\n");
		ret = PTR_ERR(xbl_data->dbg_file);
	}

put_node:
	of_node_put(parent);
	return ret;
}

static int xbl_log_remove(struct platform_device *pdev)
{
	struct xbl_log_data *xbl_data = platform_get_drvdata(pdev);

	debugfs_remove_recursive(xbl_data->dbg_file);
	return 0;
}

static struct platform_driver xbl_log_driver = {
	.probe = xbl_log_probe,
	.remove = xbl_log_remove,
	.driver = {
		   .name = "xbl-log",
		   },
};

static struct platform_device xbl_log_device = {
	.name = "xbl-log",
};

static int __init xbl_log_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&xbl_log_driver);
	if (!ret) {
		ret = platform_device_register(&xbl_log_device);
		if (ret)
			platform_driver_unregister(&xbl_log_driver);
	}
	return ret;
}

static void __exit xbl_log_exit(void)
{
	platform_device_unregister(&xbl_log_device);
	platform_driver_unregister(&xbl_log_driver);
}

module_init(xbl_log_init);
module_exit(xbl_log_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) XBL log driver");
MODULE_LICENSE("GPL");
