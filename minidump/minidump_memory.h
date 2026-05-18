/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _MINIDUMP_MEMORY_H
#define _MINIDUMP_MEMORY_H

#define MD_MEMINFO_PAGES	1
#define MD_SLABINFO_PAGES	8

int md_register_panic_entries(int num_pages, char *name,
				      struct seq_buf **global_buf);
int md_minidump_memory_init(void);
void md_dump_memory(void);

#endif /* _MINIDUMP_MEMORY_H */
