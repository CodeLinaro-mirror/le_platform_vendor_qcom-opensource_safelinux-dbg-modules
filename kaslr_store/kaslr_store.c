// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
*/

#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#define KASLR_OFFSET_BIT_MASK   0x00000000FFFFFFFF
#define KASLR_MAGIC 0xdead4ead
static int kaslr_off_store_probe(struct platform_device *pdev)
{
	void __iomem *base;
	u64 kaslr_off = kaslr_offset();

	base = devm_platform_ioremap_resource(pdev, 0);

	if (IS_ERR(base)) {
		pr_err("%s: failed to map \n", __func__);
		return PTR_ERR(base);
	}

	writel(KASLR_MAGIC, base);
	writel(kaslr_off & KASLR_OFFSET_BIT_MASK, base + 4);
	writel((kaslr_off >> 32) & KASLR_OFFSET_BIT_MASK, base + 8);

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,kaslr-off-store" },
	{ }
};
MODULE_DEVICE_TABLE(of, match_table);

static struct platform_driver kaslr_off_store_driver = {
	.probe  = kaslr_off_store_probe,
	.driver = {
		   .name = "kaslr-off-store",
		   .of_match_table = match_table,
		   .suppress_bind_attrs = true,
	},
};

static int __init kaslr_off_store_init(void)
{
	return platform_driver_register(&kaslr_off_store_driver);
}
arch_initcall(kaslr_off_store_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Kaslr offset store driver");
MODULE_LICENSE("GPL v2");
