/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __GH_RM_DRV_H
#define __GH_RM_DRV_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_GH_RM_DRV)
/* RM client registration APIs */

int gh_rm_minidump_get_info(void);
int gh_rm_minidump_register_range(phys_addr_t base_ipa, size_t region_size,
				  const char *name, size_t name_size);
int gh_rm_minidump_deregister_slot(uint16_t slot_num);

#else

/* API for minidump support */
static inline int gh_rm_minidump_get_info(void)
{
	return -EINVAL;
}

static inline int gh_rm_minidump_register_range(phys_addr_t base_ipa,
					 size_t region_size, const char *name,
					 size_t name_size)
{
	return -EINVAL;
}

static inline int gh_rm_minidump_deregister_slot(uint16_t slot_num)
{
	return -EINVAL;
}

#endif
#endif
