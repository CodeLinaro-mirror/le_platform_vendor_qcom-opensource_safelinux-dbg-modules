// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/device.h>

#include "../minidump/minidump.h"

static int build_md_region_from_reg(struct device_node *np,
				unsigned int idx, struct md_region *mdr)
{
		struct resource res;
		int ret;
		const char *dt_name = NULL;
		size_t copied;

		ret = of_address_to_resource(np, idx, &res);
		if (ret) {
			pr_err("%s: of_address_to_resource(idx=%u) failed %d\n",
					__func__, idx, ret);
			return ret;
		}

		memset(mdr, 0, sizeof(*mdr));

		ret = of_property_read_string_index(np, "reg-names", idx,
						&dt_name);
		if (ret == 0 && dt_name) {
			copied = strscpy(mdr->name, dt_name, sizeof(mdr->name));
			if (copied >= sizeof(mdr->name))
				pr_warn("%s: reg‑name > mdr->name\n", __func__);
		} else {
			/* No reg‑names entry → deterministic fallback */
			snprintf(mdr->name, sizeof(mdr->name), "fvm_region%u", idx);
		}

		mdr->phys_addr = res.start;
		/* FirmwareVM's mem does not need to be parsed by ramparser */
		mdr->virt_addr = phys_to_virt(res.start);
		mdr->size      = resource_size(&res);
		mdr->id        = 0;

		/* minidump expects 4‑byte alignment */
		mdr->size = ALIGN(mdr->size, 4);
		return 0;
}

static int register_minidump_region(struct device *dev,
									struct md_region *mdr)
{
		int ret = msm_minidump_add_region(mdr);

		if (ret >= 0) {
			dev_info(dev, "%s added to minidump (slot %d)\n", mdr->name, ret);
			return 0;
		}

		dev_err(dev, "msm_minidump_add_region(%s) failed %d\n", mdr->name, ret);
		return ret;
}

static int firmware_vm_mini_dump_probe(struct platform_device *pdev)
{
		struct device_node *np = pdev->dev.of_node;
		unsigned int addr_cells, size_cells, n_cells, n_entries;
		struct md_region *regions;
		unsigned int i;
		int ret;

		if (!np)
			return dev_err_probe(&pdev->dev, -ENODEV, "no OF node\n");

		n_cells = of_property_count_u32_elems(np, "reg");
		if (n_cells < 0)
			return dev_err_probe(&pdev->dev, -EINVAL,
					"count reg cells failed\n");

		addr_cells = of_n_addr_cells(np);
		size_cells = of_n_size_cells(np);
		if (addr_cells + size_cells == 0)
			return dev_err_probe(&pdev->dev, -EINVAL,
					"bogus address/size cell counts\n");

		n_entries = n_cells / (addr_cells + size_cells);
		if (n_entries == 0) {
			dev_info(&pdev->dev, "reg property empty\n");
			return 0;
		}

		regions = devm_kzalloc(&pdev->dev,
				n_entries * sizeof(*regions), GFP_KERNEL);
		if (!regions)
			return -ENOMEM;

		for (i = 0; i < n_entries; i++) {
			ret = build_md_region_from_reg(np, i, &regions[i]);
			if (ret)
				continue;

			ret = register_minidump_region(&pdev->dev, &regions[i]);
			if (ret)
				continue;
		}

		return 0;
}

static int firmware_vm_mini_dump_remove(struct platform_device *pdev)
{
		dev_info(&pdev->dev, "driver removed\n");
		return 0;
}

static const struct of_device_id fw_vm_mini_dt_match[] = {
		{ .compatible = "qcom,firmware-vm-mini-dump" },
		{ }
};
MODULE_DEVICE_TABLE(of, fw_vm_mini_dt_match);

static struct platform_driver fw_vm_mini_dump_driver = {
		.probe  = firmware_vm_mini_dump_probe,
		.remove = firmware_vm_mini_dump_remove,
		.driver = {
			.name           = "firmware_vm_mini_dump",
			.of_match_table = fw_vm_mini_dt_match,
		},
};
module_platform_driver(fw_vm_mini_dump_driver);

MODULE_DESCRIPTION("Qualcomm Firmware‑VM mini‑dump driver");
MODULE_LICENSE("GPL");
