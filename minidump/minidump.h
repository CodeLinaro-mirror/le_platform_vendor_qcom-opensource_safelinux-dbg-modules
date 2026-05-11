/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MINIDUMP_H
#define __MINIDUMP_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/stacktrace.h>
#include <linux/page_ext.h>
#define CONFIG_MINIDUMP_MAX_ENTRIES 400

enum minidump_entry_cmd {
	MINIDUMP_ADD,
	MINIDUMP_REMOVE,
	MINIDUMP_UPDATE,
	MINIDUMP_NO_CMD
};

#if IS_ENABLED(CONFIG_ARCH_QTI_VM)
#define MAX_NAME_LENGTH		9
#else
#define MAX_NAME_LENGTH		12
#endif

/* md_region -  Minidump table entry
 * @name:	Entry name, Minidump will dump binary with this name.
 * @id:		Entry ID, used only for SDI dumps.
 * @virt_addr:  Address of the entry.
 * @phys_addr:	Physical address of the entry to dump.
 * @size:	Number of byte to dump from @address location
 *		it should be 4 byte aligned.
 */
struct md_region {
	char	name[MAX_NAME_LENGTH + 1];
	u32	id;
	u64	virt_addr;
	u64	phys_addr;
	u64	size;
};

struct md_pending_region {
	struct list_head	list;
	struct md_region	entry;
};

struct md_rm_region {
	char	name[MAX_NAME_LENGTH + 1];
	u32	slot_num;
};

struct md_rm_request {
	struct md_region	entry;
	struct work_struct	work;
	enum minidump_entry_cmd	work_cmd;
};

/*
 * Register an entry in Minidump table
 * Returns:
 *	region number: entry position in minidump table.
 *	Negative error number on failures.
 */
extern int msm_minidump_add_region(const struct md_region *entry);
extern int msm_minidump_remove_region(const struct md_region *entry);
/*
 * Update registered region address in Minidump table.
 * It does not hold any locks, so strictly serialize the region updates.
 * Returns:
 *	Zero: on successfully update
 *	Negetive error number on failures.
 */
extern int msm_minidump_update_region(int regno, const struct md_region *entry);
extern bool msm_minidump_enabled(void);
extern struct md_region md_get_region(char *name);
extern void dump_stack_minidump(u64 sp);
extern int msm_minidump_get_available_region(void);

extern void md_dump_process(void);

#define MAX_OWNER_STRING	32
struct va_md_entry {
	unsigned long vaddr;
	unsigned char owner[MAX_OWNER_STRING];
	unsigned int size;
	void (*cb)(void *dst, unsigned long size);
};

#if IS_ENABLED(CONFIG_QCOM_VA_MINIDUMP)
extern bool qcom_va_md_enabled(void);
extern int qcom_va_md_register(const char *name, struct notifier_block *nb);
extern int qcom_va_md_unregister(const char *name, struct notifier_block *nb);
extern int qcom_va_md_add_region(struct va_md_entry *entry);
#else
static inline bool qcom_va_md_enabled(void) { return false; }
static inline int qcom_va_md_register(const char *name, struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int qcom_va_md_unregister(const char *name, struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int qcom_va_md_add_region(struct va_md_entry *entry)
{
	return -ENODEV;
}
#endif

void boot_log_dump_exit(void);
struct slabinfo;
/* Declare pointers to the functions we will be looking up via kallsyms */
typedef phys_addr_t (*per_cpu_ptr_to_phys_fn)(void *);
typedef void (*arch_stack_walk_fn)(stack_trace_consume_fn consume_entry,
		void *cookie, struct task_struct *task, struct pt_regs *regs);
typedef struct page *(*cma_alloc_fn)(struct cma *cma, unsigned long count,
					unsigned int align, bool no_warn);
typedef bool (*cma_release_fn)(struct cma *cma, const struct page *pages,
							unsigned long count);
typedef unsigned long (*pcpu_nr_pages_fn)(void);
typedef void (*get_slabinfo_fn)(struct kmem_cache *s, struct slabinfo *sinfo);
typedef void (*si_swapinfo_fn)(struct sysinfo *val);
typedef unsigned long (*vmalloc_nr_pages_fn)(void);
typedef struct page_ext *(*page_ext_get_fn) (struct page *);
typedef void (*page_ext_put_fn) (struct page_ext *);

/* Externs for globals defined in minidump_log.c */
extern per_cpu_ptr_to_phys_fn per_cpu_ptr_to_phys_t;
extern arch_stack_walk_fn arch_stack_walk_t;
extern cma_alloc_fn cma_alloc_t;
extern cma_release_fn cma_release_t;
extern pcpu_nr_pages_fn pcpu_nr_pages_t;
extern get_slabinfo_fn get_slabinfo_t;
extern si_swapinfo_fn si_swapinfo_t;
extern vmalloc_nr_pages_fn vmalloc_nr_pages_t;
extern page_ext_get_fn page_ext_get_t;
extern page_ext_put_fn page_ext_put_t;
#endif
