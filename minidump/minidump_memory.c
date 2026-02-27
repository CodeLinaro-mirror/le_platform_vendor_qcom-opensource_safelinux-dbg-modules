// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/align.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/seq_buf.h>
#include <linux/vmalloc.h>
#include <linux/cma.h>
#include <linux/slab.h>
#include <linux/page-flags.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include "minidump.h"
#include <linux/dma-map-ops.h>
#include <linux/jhash.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/fdtable.h>
#include <linux/qcom_dma_heap.h>
#include <linux/kallsyms.h>
#include "minidump_memory.h"
#include "../../../mm/slab.h"
#include "../mm/internal.h"

/* Meminfo */
static struct seq_buf *md_meminfo_seq_buf;

/* Slabinfo */
static struct seq_buf *md_slabinfo_seq_buf;

static size_t md_slabowner_dump_size = SZ_2M;

static size_t md_dma_buf_info_size = SZ_256K;

static size_t md_dma_buf_procs_size = SZ_256K;
static char *md_dma_buf_procs_addr;

static size_t md_task_memstat_size = SZ_256K;
static char *md_task_memstat_addr;

static unsigned long *md_debug_totalcma_pages;
static struct list_head *md_debug_slab_caches;
static struct mutex *md_debug_slab_mutex;
static struct static_key *md_debug_slub_debug_enabled;
static unsigned long *md_debug_min_low_pfn;
static unsigned long *md_debug_max_pfn;

#define DMA_BUF_HASH_SIZE (1 << 20)
#define DMA_BUF_HASH_SEED 0x9747b28c
static bool dma_buf_hash[DMA_BUF_HASH_SIZE];

struct priv_buf {
	char *buf;
	size_t size;
	size_t offset;
};

struct dma_buf_priv {
	struct priv_buf *priv_buf;
	struct task_struct *task;
	int count;
	size_t size;
};

static void show_val_kb(struct seq_buf *m, const char *s, unsigned long num)
{
	seq_buf_printf(m, "%s : %ld KB\n", s, num << (PAGE_SHIFT - 10));
}

static void md_dump_meminfo(struct seq_buf *m)
{
	struct sysinfo i;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	int lru;

	si_meminfo(&i);
	si_swapinfo_t(&i);

	cached = global_node_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	available = si_mem_available();
	sreclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
	sunreclaim = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);

	show_val_kb(m, "MemTotal:       ", i.totalram);
	show_val_kb(m, "MemFree:        ", i.freeram);
	show_val_kb(m, "MemAvailable:   ", available);
	show_val_kb(m, "Buffers:        ", i.bufferram);
	show_val_kb(m, "Cached:         ", cached);
	show_val_kb(m, "SwapCached:     ", total_swapcache_pages());
	show_val_kb(m, "Active:         ", pages[LRU_ACTIVE_ANON] +
					   pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive:       ", pages[LRU_INACTIVE_ANON] +
					   pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Active(anon):   ", pages[LRU_ACTIVE_ANON]);
	show_val_kb(m, "Inactive(anon): ", pages[LRU_INACTIVE_ANON]);
	show_val_kb(m, "Active(file):   ", pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive(file): ", pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Unevictable:    ", pages[LRU_UNEVICTABLE]);
	show_val_kb(m, "Mlocked:        ", global_zone_page_state(NR_MLOCK));

#ifdef CONFIG_HIGHMEM
	show_val_kb(m, "HighTotal:      ", i.totalhigh);
	show_val_kb(m, "HighFree:       ", i.freehigh);
	show_val_kb(m, "LowTotal:       ", i.totalram - i.totalhigh);
	show_val_kb(m, "LowFree:        ", i.freeram - i.freehigh);
#endif

	show_val_kb(m, "SwapTotal:      ", i.totalswap);
	show_val_kb(m, "SwapFree:       ", i.freeswap);
	show_val_kb(m, "Dirty:          ",
		    global_node_page_state(NR_FILE_DIRTY));
	show_val_kb(m, "Writeback:      ",
		    global_node_page_state(NR_WRITEBACK));
	show_val_kb(m, "AnonPages:      ",
		    global_node_page_state(NR_ANON_MAPPED));
	show_val_kb(m, "Mapped:         ",
		    global_node_page_state(NR_FILE_MAPPED));
	show_val_kb(m, "Shmem:          ", i.sharedram);
	show_val_kb(m, "KReclaimable:   ", sreclaimable +
		    global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
	show_val_kb(m, "Slab:           ", sreclaimable + sunreclaim);
	show_val_kb(m, "SReclaimable:   ", sreclaimable);
	show_val_kb(m, "SUnreclaim:     ", sunreclaim);
	seq_buf_printf(m, "KernelStack:    %8lu kB\n",
		   global_node_page_state(NR_KERNEL_STACK_KB));
#ifdef CONFIG_SHADOW_CALL_STACK
	seq_buf_printf(m, "ShadowCallStack:%8lu kB\n",
		   global_node_page_state(NR_KERNEL_SCS_KB));
#endif
	show_val_kb(m, "PageTables:     ",
		    global_node_page_state(NR_PAGETABLE));
	show_val_kb(m, "Bounce:         ",
		    global_zone_page_state(NR_BOUNCE));
	show_val_kb(m, "WritebackTmp:   ",
		    global_node_page_state(NR_WRITEBACK_TEMP));
	seq_buf_printf(m, "VmallocTotal:   %8lu kB\n",
		   (unsigned long)VMALLOC_TOTAL >> 10);
	show_val_kb(m, "VmallocUsed: ", vmalloc_nr_pages_t());
	show_val_kb(m, "Percpu:         ", pcpu_nr_pages_t());

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	show_val_kb(m, "AnonHugePages:  ",
		    global_node_page_state(NR_ANON_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "ShmemHugePages: ",
		    global_node_page_state(NR_SHMEM_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "ShmemPmdMapped: ",
		    global_node_page_state(NR_SHMEM_PMDMAPPED) * HPAGE_PMD_NR);
	show_val_kb(m, "FileHugePages:  ",
		    global_node_page_state(NR_FILE_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "FilePmdMapped:  ",
		    global_node_page_state(NR_FILE_PMDMAPPED) * HPAGE_PMD_NR);
#endif

#ifdef CONFIG_CMA
	show_val_kb(m, "CmaTotal:       ", *md_debug_totalcma_pages);
	show_val_kb(m, "CmaFree:        ",
		    global_zone_page_state(NR_FREE_CMA_PAGES));
#endif
}

#ifdef CONFIG_SLUB_DEBUG
static void slabinfo_stats(struct seq_buf *m, struct kmem_cache *cachep)
{
#ifdef CONFIG_DEBUG_SLAB
	{			/* node stats */
		unsigned long high = cachep->high_mark;
		unsigned long allocs = cachep->num_allocations;
		unsigned long grown = cachep->grown;
		unsigned long reaped = cachep->reaped;
		unsigned long errors = cachep->errors;
		unsigned long max_freeable = cachep->max_freeable;
		unsigned long node_allocs = cachep->node_allocs;
		unsigned long node_frees = cachep->node_frees;
		unsigned long overflows = cachep->node_overflow;

		seq_buf_printf(m,
				" : globalstat %7lu %6lu %5lu %4lu %4lu %4lu %4lu %4lu %4lu",
				allocs, high, grown,
				reaped, errors, max_freeable,
				node_allocs, node_frees, overflows);
	}
	/* cpu stats */
	{
		unsigned long allochit = atomic_read(&cachep->allochit);
		unsigned long allocmiss = atomic_read(&cachep->allocmiss);
		unsigned long freehit = atomic_read(&cachep->freehit);
		unsigned long freemiss = atomic_read(&cachep->freemiss);

		seq_buf_printf(m,
				" : cpustat %6lu %6lu %6lu %6lu",
				allochit, allocmiss, freehit, freemiss);
	}
#endif
}

static void md_dump_slabinfo(struct seq_buf *m)
{
	struct kmem_cache *s;
	struct slabinfo sinfo;

	if (!md_debug_slab_caches)
		return;

	if (!md_debug_slab_mutex)
		return;

	if (!mutex_trylock(md_debug_slab_mutex))
		return;

	/* print_slabinfo_header */
	seq_buf_printf(m,
			"# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>");
	seq_buf_printf(m,
			" : tunables <limit> <batchcount> <sharedfactor>");
	seq_buf_printf(m,
			" : slabdata <active_slabs> <num_slabs> <sharedavail>");
#ifdef CONFIG_DEBUG_SLAB
	seq_buf_printf(m,
			" : globalstat <listallocs> <maxobjs> <grown> <reaped> <error> <maxfreeable> <nodeallocs> <remotefrees> <alienoverflow>");
	seq_buf_printf(m,
			" : cpustat <allochit> <allocmiss> <freehit> <freemiss>");
#endif
	seq_buf_printf(m, "\n");

	/* Loop through all slabs */
	list_for_each_entry(s, md_debug_slab_caches, list) {
		memset(&sinfo, 0, sizeof(sinfo));
		get_slabinfo_t(s, &sinfo);

		seq_buf_printf(m, "%-17s %6lu %6lu %6u %4u %4d",
		   s->name, sinfo.active_objs, sinfo.num_objs, s->size,
		   sinfo.objects_per_slab, (1 << sinfo.cache_order));

		seq_buf_printf(m, " : tunables %4u %4u %4u",
		   sinfo.limit, sinfo.batchcount, sinfo.shared);
		seq_buf_printf(m, " : slabdata %6lu %6lu %6lu",
		   sinfo.active_slabs, sinfo.num_slabs, sinfo.shared_avail);
		slabinfo_stats(m, s);
		seq_buf_printf(m, "\n");
	}
	mutex_unlock(md_debug_slab_mutex);
}
#else
static inline void md_dump_slabinfo(void) {}
#endif /* CONFIG_SLUB_DEBUG */

static struct cma *dma_contiguous_default_area_t;

bool md_register_memory_dump(int size, char *name)
{
	struct md_region md_entry;
	void *buffer_start;
	struct page *page;
	int ret;
	struct cma **dma_contiguous_default_sym;

	dma_contiguous_default_sym =
			(struct cma **)kallsyms_lookup_name("dma_contiguous_default_area");
	if (!dma_contiguous_default_sym) {
		pr_err("minidump: Unable to find symbol \'dma_contiguous_default_area\'\n");
		return false;
	}

	dma_contiguous_default_area_t = *dma_contiguous_default_sym;
	page  = cma_alloc_t(dma_contiguous_default_area_t, size >> PAGE_SHIFT, 0, GFP_KERNEL);
	if (!page) {
		pr_err("Failed to allocate %s minidump, increase cma size\n", name);
		return false;
	}

	buffer_start = page_to_virt(page);
	strscpy(md_entry.name, name, sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t) buffer_start;
	md_entry.phys_addr = virt_to_phys(buffer_start);
	md_entry.size = size;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0) {
		cma_release_t(dma_contiguous_default_area_t, page, size >> PAGE_SHIFT);
		pr_err("Failed to add %s entry in Minidump\n", name);
		return false;
	}

	memset(buffer_start, 0, size);
	/* Complete registration before adding enteries */
	smp_mb();
	if (!strcmp(name, "DMA_PROC"))
		WRITE_ONCE(md_dma_buf_procs_addr, buffer_start);

	if (!strcmp(name, "TASK_MEMSTAT"))
		WRITE_ONCE(md_task_memstat_addr, buffer_start);

	return true;
}

bool md_unregister_memory_dump(char *name)
{
	struct page *page;
	struct md_region mdr;
	struct md_region md_entry;

	mdr = md_get_region(name);
	if (!mdr.virt_addr) {
		pr_err("minidump entry for %s not found\n", name);
		return false;
	}
	strscpy(md_entry.name, mdr.name, sizeof(md_entry.name));
	md_entry.virt_addr = mdr.virt_addr;
	md_entry.phys_addr = mdr.phys_addr;
	md_entry.size = mdr.size;
	page = virt_to_page(mdr.virt_addr);

	if (msm_minidump_remove_region(&md_entry) < 0)
		return false;

	cma_release_t(dma_contiguous_default_area_t, page,
			(md_entry.size) >> PAGE_SHIFT);
	return true;
}

static void update_dump_size(char *name, size_t size, char **addr, size_t *dump_size)
{
	if ((*dump_size) == 0) {
		if (md_register_memory_dump(size * SZ_1M,
						name)) {
			*dump_size = size * SZ_1M;
			pr_info_ratelimited("%s Minidump set to %zd MB size\n",
					name, size);
		}
		return;
	}
	if (md_unregister_memory_dump(name)) {
		*addr = NULL;
		if (size == 0) {
			*dump_size = 0;
			pr_info_ratelimited("%s Minidump : disabled\n", name);
			return;
		}
		if (md_register_memory_dump(size * SZ_1M,
						name)) {
			*dump_size = size * SZ_1M;
			pr_info_ratelimited("%s Minidump : set to %zd MB\n",
					name, size);
		} else if (md_register_memory_dump(*dump_size,
							name)) {
			pr_info_ratelimited("%s Minidump : Fallback to %zd MB\n",
					name, (*dump_size) / SZ_1M);
		} else {
			pr_err_ratelimited("%s Minidump : disabled, Can't fallback to %zd MB,\n",
						name, (*dump_size) / SZ_1M);
			*dump_size = 0;
		}
	} else {
		pr_err_ratelimited("Failed to unregister %s Minidump\n", name);
	}
}

#ifdef CONFIG_SLUB_DEBUG
#define STACK_HASH_SEED 0x9747b28c

static unsigned long slab_owner_filter;
static unsigned long slab_owner_handles_size = SZ_16K;

static bool is_slub_debug_enabled(void)
{
	if (md_debug_slub_debug_enabled &&
		atomic_read(&md_debug_slub_debug_enabled->enabled))
		return true;
	return false;
}

static ssize_t slab_owner_filter_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long filter;
	int bit, i;
	struct kmem_cache *s;

	if (kstrtoul_from_user(ubuf, count, 0, &filter)) {
		pr_err_ratelimited("Invalid format for filter\n");
		return -EINVAL;
	}

	for (i = 0, bit = 1; filter >= bit; bit *= 2, i++) {
		if (filter & bit) {
			s = kmalloc_caches[KMALLOC_NORMAL][i];
			if (!s) {
				pr_err("Invalid filter : %lx kmalloc-%d doesn't exist\n",
						filter, bit);
				return -EINVAL;
			}
		}
	}
	slab_owner_filter = filter;
	return count;
}

static ssize_t slab_owner_filter_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "0x%lx\n", slab_owner_filter);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_slab_owner_filter_ops = {
	.open	= simple_open,
	.write	= slab_owner_filter_write,
	.read	= slab_owner_filter_read,
};

static ssize_t slab_owner_handle_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long size;

	if (kstrtoul_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for handle size\n");
		return -EINVAL;
	}

	if (size) {
		if (size > (md_slabowner_dump_size / SZ_16K)) {
			pr_err_ratelimited("size : %lu KB exceeds max size : %lu KB\n",
				size, (md_slabowner_dump_size / SZ_16K));
			goto err;
		}
		slab_owner_handles_size = size * SZ_1K;
	}
err:
	return count;
}

static ssize_t slab_owner_handle_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "%lu KB\n",
			(slab_owner_handles_size / SZ_1K));
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_slab_owner_handle_ops = {
	.open	= simple_open,
	.write	= slab_owner_handle_write,
	.read	= slab_owner_handle_read,
};

static void md_debugfs_slabowner(struct dentry *minidump_dir)
{
	int i;

	debugfs_create_file("slab_owner_filter", 0400, minidump_dir, NULL,
		    &proc_slab_owner_filter_ops);
	debugfs_create_file("slab_owner_handles_size_kb", 0400,
			minidump_dir, NULL, &proc_slab_owner_handle_ops);
	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		if (kmalloc_caches[KMALLOC_NORMAL][i])
			set_bit(i, &slab_owner_filter);
	}
}
#else
static inline bool is_slub_debug_enabled(void)
{
	return false;
}
static inline void md_debugfs_slabowner(struct dentry *minidump_dir) {}
#endif	/* CONFIG_SLUB_DEBUG */

static int get_dma_info(const void *data, struct file *file, unsigned int n)
{
	struct priv_buf *buf;
	struct dma_buf_priv *dma_buf_priv;
	struct dma_buf *dmabuf;
	struct task_struct *task;
	int ret;
	u32 index;

	if (!qcom_is_dma_buf_file(file))
		return 0;

	dma_buf_priv = (struct dma_buf_priv *)data;
	buf = dma_buf_priv->priv_buf;
	task = dma_buf_priv->task;
	if (dma_buf_priv->count == 0) {
		ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
				"\n%s (PID %d)\nDMA Buffers:\n",
				task->comm, task->tgid);
		buf->offset += ret;
		if (buf->offset == buf->size - 1)
			return -EINVAL;
	}
	dmabuf = (struct dma_buf *)file->private_data;
	index = jhash(dmabuf, sizeof(struct dma_buf), DMA_BUF_HASH_SEED);
	index = index  & (DMA_BUF_HASH_SIZE - 1);
	if (dma_buf_hash[index])
		return 0;
	dma_buf_hash[index] = true;
	dma_buf_priv->count += 1;
	ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
			"%-8s\t%-8s\t%-8s\t%-8s\texp_name\t%-8s\n",
			"size", "flags", "mode", "count", "ino");
	buf->offset += ret;
	if (buf->offset == buf->size - 1)
		return -EINVAL;
	ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
			"%08zu\t%08x\t%08x\t%08ld\t%s\t%08lu\t%s\n",
			dmabuf->size,
			dmabuf->file->f_flags, dmabuf->file->f_mode,
			file_count(dmabuf->file),
			dmabuf->exp_name,
			file_inode(dmabuf->file)->i_ino,
			dmabuf->name ?: "");
	buf->offset += ret;
	if (buf->offset == buf->size - 1)
		return -EINVAL;
	dma_buf_priv->size += dmabuf->size;
	return 0;
}

static void md_dma_buf_procs(char *m, size_t dump_size)
{
	struct task_struct *task, *thread;
	struct files_struct *files;
	int ret = 0;
	struct priv_buf buf;
	struct dma_buf_priv dma_buf_priv;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;
	dma_buf_priv.priv_buf = &buf;
	dma_buf_priv.count = 0;
	dma_buf_priv.size = 0;

	rcu_read_lock();
	for_each_process(task) {
		struct files_struct *group_leader_files = NULL;

		dma_buf_priv.task = task;
		for_each_thread(task, thread) {
			task_lock(thread);
			if (unlikely(!group_leader_files))
				group_leader_files = task->group_leader->files;
			files = thread->files;
			if (files && (group_leader_files != files ||
				      thread == task->group_leader))
				ret = iterate_fd(files, 0, get_dma_info, &dma_buf_priv);
			task_unlock(thread);
			if (ret)
				goto err;
		}
		if (dma_buf_priv.count) {
			ret = scnprintf(buf.buf + buf.offset, buf.size - buf.offset,
				"\nTotal %d objects, %zu bytes\n",
				dma_buf_priv.count, dma_buf_priv.size);
			buf.offset += ret;
			if (buf.offset == buf.size - 1)
				goto err;
			dma_buf_priv.count = 0;
			dma_buf_priv.size = 0;
			memset(dma_buf_hash, 0, sizeof(dma_buf_hash));
		}
	}
	rcu_read_unlock();
	return;
err:
	rcu_read_unlock();
	pr_err("DMABUF_PROCS Minidump region exhausted\n");
}

static ssize_t dma_buf_procs_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long  size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	update_dump_size("DMA_PROC", size,
			&md_dma_buf_procs_addr, &md_dma_buf_procs_size);
	return count;
}

static ssize_t dma_buf_procs_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%zu MB\n", md_dma_buf_procs_size/SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_dma_buf_procs_size_ops = {
	.open	= simple_open,
	.write	= dma_buf_procs_size_write,
	.read	= dma_buf_procs_size_read,
};

static void md_debugfs_dmabufprocs(struct dentry *minidump_dir)
{
	debugfs_create_file("dma_buf_procs_size_mb", 0400, minidump_dir, NULL,
			&proc_dma_buf_procs_size_ops);
}

static void md_task_memstat(char *m, size_t dump_size)
{
	struct task_struct *task;
	struct priv_buf buf;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;

	buf.offset += scnprintf(buf.buf + buf.offset,
				buf.size - buf.offset,
				"%-8s %-8s %-10s %-8s %-16s %-16s %-16s %s\n",
				"PID",
				"RSS(KB)",
				"SWAP(KB)",
				"ADJ",
				"anon_rss(KB)",
				"file_rss(KB)",
				"shmem_rss(KB)",
				"TaskName");

	rcu_read_lock();
	for_each_process(task) {
		if (task->mm) {
			buf.offset += scnprintf(buf.buf + buf.offset,
					buf.size - buf.offset,
					"%-8d %-8lu %-10lu %-8d %-16lu %-16lu %-16lu %s\n",
					task->pid,
					K(get_mm_rss(task->mm)),
					K(get_mm_counter(task->mm, MM_SWAPENTS)),
					task->signal->oom_score_adj,
					K(get_mm_counter(task->mm, MM_ANONPAGES)),
					K(get_mm_counter(task->mm, MM_FILEPAGES)),
					K(get_mm_counter(task->mm, MM_SHMEMPAGES)),
					task->comm);
			if (buf.offset == buf.size - 1)
				goto err;
		}
	}
	rcu_read_unlock();

	return;
err:
	rcu_read_unlock();
	pr_err("TASK_MEMSTAT Minidump region exhausted\n");
}

static ssize_t task_memstat_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	update_dump_size("TASK_MEMSTAT", size,
			&md_task_memstat_addr, &md_task_memstat_size);
	return count;
}

static ssize_t task_memstat_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%zu MB\n", md_task_memstat_size/SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_task_memstat_size_ops = {
	.open   = simple_open,
	.write  = task_memstat_size_write,
	.read   = task_memstat_size_read,
};

static void md_debugfs_task_memstat(struct dentry *minidump_dir)
{
	debugfs_create_file("task_memstat_size_mb", 0400, minidump_dir, NULL,
			&proc_task_memstat_size_ops);
}

void md_dump_memory(void)
{
	if (md_meminfo_seq_buf)
		md_dump_meminfo(md_meminfo_seq_buf);

	if (md_slabinfo_seq_buf)
		md_dump_slabinfo(md_slabinfo_seq_buf);

	if (md_dma_buf_procs_addr)
		md_dma_buf_procs(md_dma_buf_procs_addr, md_dma_buf_procs_size);

	if (md_task_memstat_addr)
		md_task_memstat(md_task_memstat_addr, md_task_memstat_size);
}

#define MD_KALLSYMS_LOOKUP(_var, type) \
	do { \
		md_debug_##_var = (type *)kallsyms_lookup_name(#_var); \
		if (!md_debug_##_var) { \
			pr_err("minidump: %s symbol not available in kernel\n", #_var); \
			error |= 1; \
		} \
	} while (0)

int md_minidump_memory_init(void)
{
	int error = 0;
	struct dentry *minidump_dir = NULL;

	MD_KALLSYMS_LOOKUP(totalcma_pages, unsigned long);
	MD_KALLSYMS_LOOKUP(slab_caches, struct list_head);
	MD_KALLSYMS_LOOKUP(slab_mutex, struct mutex);
	MD_KALLSYMS_LOOKUP(slub_debug_enabled, struct static_key);
	MD_KALLSYMS_LOOKUP(min_low_pfn, unsigned long);
	MD_KALLSYMS_LOOKUP(max_pfn, unsigned long);
	/* error set by MD_KALLSYMS_LOOKUP */
	if (error)
		return error;

	minidump_dir = debugfs_create_dir("minidump", NULL);
	md_register_panic_entries(MD_MEMINFO_PAGES, "MEMINFO",
				  &md_meminfo_seq_buf);
#ifdef CONFIG_SLUB_DEBUG
	md_register_panic_entries(MD_SLABINFO_PAGES, "SLABINFO",
				  &md_slabinfo_seq_buf);
#endif
	if (is_slub_debug_enabled()) {
		md_register_memory_dump(md_slabowner_dump_size, "SLABOWNER");
		md_debugfs_slabowner(minidump_dir);
	}

	md_register_memory_dump(md_dma_buf_info_size, "DMA_INFO");
	md_register_memory_dump(md_dma_buf_procs_size, "DMA_PROC");
	md_debugfs_dmabufprocs(minidump_dir);
	md_register_memory_dump(md_task_memstat_size, "TASK_MEMSTAT");
	md_debugfs_task_memstat(minidump_dir);
	return error;
}
MODULE_IMPORT_NS(DMA_BUF);
