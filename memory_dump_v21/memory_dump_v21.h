/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012, 2014-2017, 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __MSM_MEMORY_DUMP_H
#define __MSM_MEMORY_DUMP_H

#include <linux/errno.h>
#include <linux/types.h>

enum dump_client_type {
	MSM_CPU_CTXT = 0,
	MSM_L1_CACHE,
	MSM_L2_CACHE,
	MSM_OCMEM,
	MSM_TMC_ETFETB,
	MSM_ETM0_REG,
	MSM_ETM1_REG,
	MSM_ETM2_REG,
	MSM_ETM3_REG,
	MSM_TMC0_REG, /* TMC_ETR */
	MSM_TMC1_REG, /* TMC_ETF */
	MSM_LOG_BUF,
	MSM_LOG_BUF_FIRST_IDX,
	MAX_NUM_CLIENTS,
};

struct msm_client_dump {
	enum dump_client_type id;
	unsigned long start_addr;
	unsigned long end_addr;
};

extern uint32_t msm_dump_table_version(void);

#define MSM_DUMP_MAKE_VERSION(ma, mi)	((ma << 20) | mi)
#define MSM_DUMP_MAJOR(val)		(val >> 20)
#define MSM_DUMP_MINOR(val)		(val & 0xFFFFF)

#define MAX_NUM_ENTRIES		0x400

enum msm_dump_data_ids {
	MSM_DUMP_DATA_CPU_CTX = 0x00,			/* Per CPU entries */
	MSM_DUMP_DATA_CPU_CTX_1 = 0x10,			/* Per CPU entries */
	MSM_DUMP_DATA_L1_INST_TLB = 0x20,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_INST_TLB_1 = 0x30,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_DATA_TLB = 0x40,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_DATA_TLB_1 = 0x50,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_INST_CACHE = 0x60,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_INST_CACHE_1 = 0x70,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_DATA_CACHE = 0x80,		/* Per CPU entries */
	MSM_DUMP_DATA_L1_DATA_CACHE_1 = 0x90,		/* Per CPU entries */
	MSM_DUMP_DATA_ETM_REG = 0xA0,			/* Per CPU entries */
	MSM_DUMP_DATA_L2_CACHE = 0xC0,			/* Per L2 entries */
	MSM_DUMP_DATA_L3_CACHE = 0xD0,			/* Per L3 entries */
	MSM_DUMP_DATA_OCMEM = 0xE0,			/* Single OCMEM */
	MSM_DUMP_DATA_CNSS_WLAN = 0xE1,			/* CNSS Ramdump Entry */
	MSM_DUMP_DATA_PMIC = 0xE4,			/* PMIC Register Dump */
	MSM_DUMP_DATA_DBGUI_REG = 0xE5,			/* DebugUI Entry */
	MSM_DUMP_DATA_DCC_REG = 0xE6,			/* DCC Register Dump */
	MSM_DUMP_DATA_DCC_SRAM = 0xE7,			/* DCC SRAM Dump */
	MSM_DUMP_DATA_MISC = 0xE8,			/* Misc Register Dump */
	MSM_DUMP_DATA_VSENSE = 0xE9,			/* Vsense Register Dump */
	MSM_DUMP_DATA_RPM = 0xEA,			/* RPM Code RAM */
	MSM_DUMP_DATA_SCANDUMP = 0xEB,			/* Scan Dump Entry */
	MSM_DUMP_DATA_RPMH = 0xEC,			/* RPM HW Dump */
	MSM_DUMP_DATA_POWER_REGS_DUMP = 0xED,		/* MSM Power Register Dump */
	MSM_DUMP_DATA_CPUSS = 0xEF,			/* CPUSS Register Dump */
	MSM_DUMP_DATA_TMC_ETF = 0xF0,			/* ETF entry for QDSS */
	MSM_DUMP_DATA_TMC_ETF_SWAO = 0xF1,		/* ETF entry for SWAO */
	MSM_DUMP_DATA_TPDM_SWAO_MCMB= 0xF2,		/* TPDM MSR Registers */
	MSM_DUMP_DATA_TMC_ETF_SLPI = 0xF3,		/* ETF entry for SLPI */
	MSM_DUMP_DATA_TMC_ETF_LPASS = 0xF4,		/* ETF entry for LPASS */
	MSM_DUMP_DATA_TMC_ETR_REG = 0x100,		/* TMC-ETR Registers */
	MSM_DUMP_DATA_TMC_ETF_REG = 0x101,		/* TMC-ETF Registers */
	MSM_DUMP_DATA_TMC_ETF_SWAO_REG = 0x102,		/* TMC-ETF-SWAO Registers */
	MSM_DUMP_DATA_TMC_ETF_SLPI_REG = 0x103,		/* SLPI ETR Registers */
	MSM_DUMP_DATA_TMC_ETF_LPASS_REG = 0x104,	/* LPASS ETR Registers */
	MSM_DUMP_DATA_LOG_BUF = 0x110,			/* Kernel Log Buf */
	MSM_DUMP_DATA_LOG_BUF_FIRST_IDX = 0x111,	/* Kernel Log Buf Index */
	MSM_L2_TLB_DUMP = 0x120,			/* L2 TLB Dump */
	MSM_DUMP_DATA_LLCC2_DATA_CACHE = 0x121,		/* LLCC2 Data Cache Dump */
	MSM_DUMP_DATA_LLCC3_DATA_CACHE = 0x122,		/* LLCC3 Data Cache Dump */
	MSM_DUMP_DATA_LLCC4_DATA_CACHE = 0x123,		/* LLCC4 Data Cache Dump */
	MSM_DUMP_DATA_SCANDUMP_PER_CPU = 0x130,		/* Per Cpu Scandump */
	MSM_LLCC_DUMP = 0x140,				/* LLCC Cache Dump */
	MSM_DUMP_DATA_IPA = 0x150,			/* IPA Dump */
	MSM_DATA_APPS_MHM_SCANDUMP = 0x161,		/* APPS MHM Scandump */
	MSM_DATA_NOC_DUMP = 0x162,			/* NOC Dump */
	MSM_DUMP_DATA_IDS_VM_CTX  = 0x163,		/* HYP Virtual Machine Context */
	MSM_DUMP_DATA_CPUSS_SPR	= 0x1F0,		/* CPU SPR Dump */
	MSM_DUMP_DATA_ALL_CPU_CACHES = 0x230,		/* ALL CPU Cache Dump */
	MSM_DUMP_DATA_ALL_CLUSTER_CACHES = 0x240,	/* All Cluster Cache Dump */
	MSM_DUMP_DATA_MSM_CPU_CONTEXT = 0x250,		/* CPU context */
	MSM_DUMP_DATA_MAX = MAX_NUM_ENTRIES
};

enum msm_dump_table_ids {
	MSM_DUMP_TABLE_APPS,
	MSM_DUMP_TABLE_HYP,
	MSM_DUMP_TABLE_MAX = MAX_NUM_ENTRIES,
};

enum msm_dump_type {
	MSM_DUMP_TYPE_DATA,
	MSM_DUMP_TYPE_TABLE,
};

struct msm_dump_data {
	uint32_t version;
	uint32_t magic;
	char name[32];
	uint64_t addr;
	uint64_t len;
	uint32_t reserved;
};

struct msm_dump_entry {
	uint32_t id;
	char name[32];
	uint32_t type;
	uint64_t addr;
	uint64_t instance;
};

extern int msm_dump_data_register(enum msm_dump_table_ids id,
				  struct msm_dump_entry *entry);
extern int msm_dump_data_register_nominidump(enum msm_dump_table_ids id,
				  struct msm_dump_entry *entry);
#endif
