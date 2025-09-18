// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cache.h>
#include <linux/freezer.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include "minidump.h"
#include <asm/page.h>
#include <asm/memory.h>
#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <linux/mm.h>
#include <linux/ratelimit.h>
#include <linux/notifier.h>
#include <linux/sizes.h>
#include <linux/sched/task.h>
#include <linux/suspend.h>
#include <linux/vmalloc.h>
#include <linux/panic_notifier.h>
#include <linux/percpu.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>

#include <linux/bits.h>
#include <linux/sched/prio.h>
#include <linux/seq_buf.h>

#include <asm/memory.h>

#include <linux/kdebug.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>

#include <linux/module.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>
#include "minidump_memory.h"

#include <linux/kmsg_dump.h>

#include <trace/events/sched.h>
#ifdef CONFIG_VMAP_STACK
#define STACK_NUM_PAGES (THREAD_SIZE / PAGE_SIZE)
#else
#define STACK_NUM_PAGES 1
#endif	/* !CONFIG_VMAP_STACK */

struct md_stack_cpu_data {
	int stack_mdidx[STACK_NUM_PAGES];
	struct md_region stack_mdr[STACK_NUM_PAGES];
} ____cacheline_aligned_in_smp;

static int md_current_stack_init __read_mostly;

static DEFINE_PER_CPU_SHARED_ALIGNED(struct md_stack_cpu_data, md_stack_data);

struct md_suspend_context_data {
	int task_mdno;
	int stack_mdidx[STACK_NUM_PAGES];
	struct md_region stack_mdr[STACK_NUM_PAGES];
	struct md_region task_mdr;
	bool init;
};

static struct md_suspend_context_data md_suspend_context;

static bool is_vmap_stack __read_mostly;

#include <linux/ring_buffer.h>

#define MD_FTRACE_BUF_SIZE	SZ_2M

static char *md_ftrace_buf_addr;

#define MD_RUNQUEUE_PAGES	150

static bool md_in_oops_handler;
static atomic_t md_handle_done;
static struct seq_buf *md_runq_seq_buf;
static int md_align_offset;

/* CPU context information */
#define MD_CPU_CNTXT_PAGES	32
static int die_cpu = -1;
static struct seq_buf *md_cntxt_seq_buf;

/* Meminfo */
static struct seq_buf *md_meminfo_seq_buf;

/* Modules information */
#ifdef CONFIG_MODULES
#define MD_MODULE_PAGES	  8
static struct seq_buf *md_mod_info_seq_buf;
static DEFINE_SPINLOCK(md_modules_lock);

static int n_modump;
static char *key_modules[10];
module_param_array(key_modules, charp, &n_modump, 0644);
#endif	/* CONFIG_MODULES */

void *kmsg_buf;
/* Allocate 256KB for DMESG buffer as default value */
unsigned long kmsg_dump_sz  = (256 * 1024);

static int register_stack_entry(struct md_region *ksp_entry, u64 sp, u64 size)
{
	struct page *sp_page;
	int entry;

	ksp_entry->virt_addr = sp;
	ksp_entry->size = size;
	if (is_vmap_stack) {
		sp_page = vmalloc_to_page((const void *) sp);
		ksp_entry->phys_addr = page_to_phys(sp_page);
	} else {
		ksp_entry->phys_addr = virt_to_phys((uintptr_t *)sp);
	}

	entry = msm_minidump_add_region(ksp_entry);
	if (entry < 0)
		pr_info("Failed to add stack of entry %s in Minidump\n",
				ksp_entry->name);
	return entry;
}

static inline bool in_stack_range(
		u64 sp, u64 base_addr, unsigned int stack_size)
{
	u64 min_addr = base_addr;
	u64 max_addr = base_addr + stack_size;

	return (min_addr <= sp && sp < max_addr);
}

static unsigned int calculate_copy_pages(u64 sp, struct vm_struct *stack_area)
{
	u64 tsk_stack_base = (u64) stack_area->addr;
	u64 offset;
	unsigned int stack_pages, copy_pages;

	if (in_stack_range(sp, tsk_stack_base, get_vm_area_size(stack_area))) {
		offset = sp - tsk_stack_base;
		stack_pages = get_vm_area_size(stack_area) / PAGE_SIZE;
		copy_pages = stack_pages - (offset / PAGE_SIZE);
	} else {
		copy_pages = 0;
	}
	return copy_pages;
}

void dump_stack_minidump(u64 sp)
{
	struct md_region ksp_entry, ktsk_entry;
	u32 cpu = smp_processor_id();
	struct vm_struct *stack_vm_area;
	unsigned int i, copy_pages;

	if (IS_ENABLED(CONFIG_QCOM_DYN_MINIDUMP_STACK))
		return;

	if (is_idle_task(current))
		return;

	is_vmap_stack = IS_ENABLED(CONFIG_VMAP_STACK);

	if (sp < KIMAGE_VADDR || sp > -256UL)
		sp = current_stack_pointer;

	/*
	 * Since stacks are now allocated with vmalloc, the translation to
	 * physical address is not a simple linear transformation like it is
	 * for kernel logical addresses, since vmalloc creates a virtual
	 * mapping. Thus, virt_to_phys() should not be used in this context;
	 * instead the page table must be walked to acquire the physical
	 * address of one page of the stack.
	 */
	stack_vm_area = task_stack_vm_area(current);
	if (is_vmap_stack) {
		sp &= ~(PAGE_SIZE - 1);
		copy_pages = calculate_copy_pages(sp, stack_vm_area);
		for (i = 0; i < copy_pages; i++) {
			scnprintf(ksp_entry.name, sizeof(ksp_entry.name),
				  "KSTACK%d_%d", cpu, i);
			(void)register_stack_entry(&ksp_entry, sp, PAGE_SIZE);
			sp += PAGE_SIZE;
		}
	} else {
		sp &= ~(THREAD_SIZE - 1);
		scnprintf(ksp_entry.name, sizeof(ksp_entry.name), "KSTACK%d",
			  cpu);
		(void)register_stack_entry(&ksp_entry, sp, THREAD_SIZE);
	}

	scnprintf(ktsk_entry.name, sizeof(ktsk_entry.name), "KTASK%d", cpu);
	ktsk_entry.virt_addr = (u64)current;
	ktsk_entry.phys_addr = virt_to_phys((uintptr_t *)current);
	ktsk_entry.size = sizeof(struct task_struct);
	if (msm_minidump_add_region(&ktsk_entry) < 0)
		pr_err("Failed to add current task %d in Minidump\n", cpu);
}

static void update_stack_entry(struct md_region *ksp_entry, u64 sp,
			       int mdno)
{
	struct page *sp_page;

	ksp_entry->virt_addr = sp;
	if (likely(is_vmap_stack)) {
		sp_page = vmalloc_to_page((const void *) sp);
		ksp_entry->phys_addr = page_to_phys(sp_page);
	} else {
		ksp_entry->phys_addr = virt_to_phys((uintptr_t *)sp);
	}
	if (msm_minidump_update_region(mdno, ksp_entry) < 0) {
		pr_info("Failed to update stack entry %s in minidump\n",
			ksp_entry->name);
	}
}

static void register_vmapped_stack(struct md_region *mdr, int *mdno,
				   u64 sp, char *name_str, bool update)
{
	int i;

	sp &= ~(PAGE_SIZE - 1);
	for (i = 0; i < STACK_NUM_PAGES; i++) {
		if (unlikely(!update)) {
			scnprintf(mdr->name, sizeof(mdr->name), "%s_%d",
					  name_str, i);
			*mdno = register_stack_entry(mdr, sp, PAGE_SIZE);
		} else {
			update_stack_entry(mdr, sp, *mdno);
		}
		sp += PAGE_SIZE;
		mdr++;
		mdno++;
	}
}

static void register_normal_stack(struct md_region *mdr, int *mdno,
				  u64 sp, char *name_str, bool update)
{
	sp &= ~(THREAD_SIZE - 1);
	if (unlikely(!update)) {
		scnprintf(mdr->name, sizeof(mdr->name), name_str);
		*mdno = register_stack_entry(mdr, sp, THREAD_SIZE);
	} else {
		update_stack_entry(mdr, sp, *mdno);
	}
}

static void update_md_stack(struct md_region *stack_mdr,
			    int *stack_mdno, u64 sp)
{
	unsigned int i;
	int *mdno;

	if (likely(is_vmap_stack)) {
		for (i = 0; i < STACK_NUM_PAGES; i++) {
			mdno = stack_mdno + i;
			if (unlikely(*mdno < 0))
				return;
		}
		register_vmapped_stack(stack_mdr, stack_mdno, sp, NULL, true);
	} else {
		if (unlikely(*stack_mdno < 0))
			return;
		register_normal_stack(stack_mdr, stack_mdno, sp, NULL, true);
	}
}

static void update_md_cpu_stack(struct task_struct *tsk, u32 cpu, u64 sp)
{
	struct md_stack_cpu_data *md_stack_cpu_d = &per_cpu(md_stack_data, cpu);

	if (is_idle_task(tsk) || !md_current_stack_init)
		return;

	update_md_stack(md_stack_cpu_d->stack_mdr,
			md_stack_cpu_d->stack_mdidx, sp);
}

void md_current_stack_notifer(void *ignore, bool preempt,
		struct task_struct *prev, struct task_struct *next)
{
	u32 cpu = task_cpu(next);
	u64 sp = (u64)next->stack;

	update_md_cpu_stack(next, cpu, sp);
}

void md_current_stack_ipi_handler(void *data)
{
	u32 cpu = smp_processor_id();
	struct vm_struct *stack_vm_area;
	u64 sp = current_stack_pointer;

	if (is_idle_task(current))
		return;
	if (likely(is_vmap_stack)) {
		stack_vm_area = task_stack_vm_area(current);
		sp = (u64)stack_vm_area->addr;
	}
	update_md_cpu_stack(current, cpu, sp);
}

static void update_md_current_task(struct md_region *mdr, int mdno)
{
	mdr->virt_addr = (u64)current;
	mdr->phys_addr = virt_to_phys((uintptr_t *)current);
	if (msm_minidump_update_region(mdno, mdr) < 0)
		pr_err("Failed to update %s current task in minidump\n",
			   mdr->name);
}

static void update_md_suspend_current_stack(void)
{
	u64 sp = current_stack_pointer;
	struct vm_struct *stack_vm_area;

	if (likely(is_vmap_stack)) {
		stack_vm_area = task_stack_vm_area(current);
		sp = (u64)stack_vm_area->addr;
	}
	update_md_stack(md_suspend_context.stack_mdr,
			md_suspend_context.stack_mdidx, sp);
}

static void update_md_suspend_current_task(void)
{
	if (unlikely(md_suspend_context.task_mdno < 0))
		return;
	update_md_current_task(&md_suspend_context.task_mdr,
			md_suspend_context.task_mdno);
}

static void update_md_suspend_currents(void)
{
	if (!md_suspend_context.init)
		return;
	update_md_suspend_current_stack();
	update_md_suspend_current_task();
}

static void register_suspend_stack(void)
{
	char name_str[MAX_NAME_LENGTH];
	u64 sp = current_stack_pointer;
	struct vm_struct *stack_vm_area = task_stack_vm_area(current);

	scnprintf(name_str, sizeof(name_str), "KSUSPSTK");
	if (is_vmap_stack) {
		sp = (u64)stack_vm_area->addr;
		register_vmapped_stack(md_suspend_context.stack_mdr,
				md_suspend_context.stack_mdidx,
				sp, name_str, false);
	} else {
		register_normal_stack(md_suspend_context.stack_mdr,
			md_suspend_context.stack_mdidx,
			sp, name_str, false);
	}
}

static void register_current_task(struct md_region *mdr, int *mdno,
				  char *name_str)
{
	scnprintf(mdr->name, sizeof(mdr->name), name_str);
	mdr->virt_addr = (u64)current;
	mdr->phys_addr = virt_to_phys((uintptr_t *)current);
	mdr->size = sizeof(struct task_struct);
	*mdno = msm_minidump_add_region(mdr);
	if (*mdno < 0)
		pr_err("Failed to add current task %s in Minidump\n",
		       mdr->name);
}

static void register_suspend_current_task(void)
{
	char name_str[MAX_NAME_LENGTH];

	scnprintf(name_str, sizeof(name_str), "KSUSPTASK");
	register_current_task(&md_suspend_context.task_mdr,
			&md_suspend_context.task_mdno, name_str);
}

static int minidump_pm_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		update_md_suspend_currents();
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block minidump_pm_nb = {
	.notifier_call = minidump_pm_notifier,
};

static void register_suspend_context(void)
{
	register_suspend_stack();
	register_suspend_current_task();
	register_pm_notifier(&minidump_pm_nb);
	md_suspend_context.init = true;
}

static void md_register_trace_buf(void)
{
	struct md_region md_entry;
	void *buffer_start;

	buffer_start = kzalloc(MD_FTRACE_BUF_SIZE, GFP_KERNEL);

	if (!buffer_start)
		return;

	strscpy(md_entry.name, "KFTRACE", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)buffer_start;
	md_entry.phys_addr = virt_to_phys(buffer_start);
	md_entry.size = MD_FTRACE_BUF_SIZE;
	if (msm_minidump_add_region(&md_entry) < 0)
		pr_err("Failed to add ftrace buffer entry in Minidump\n");

	/* Complete registration before adding enteries */
	smp_mb();
	WRITE_ONCE(md_ftrace_buf_addr, buffer_start);
}

static void md_dump_align(void)
{
	int tab_offset = md_align_offset;

	while (tab_offset--)
		seq_buf_printf(md_runq_seq_buf, " | ");
	seq_buf_printf(md_runq_seq_buf, " |--");
}

static void md_dump_task_info(struct task_struct *task, char *status,
			      struct task_struct *curr)
{
	struct sched_entity *se;

	md_dump_align();
	if (!task) {
		seq_buf_printf(md_runq_seq_buf, "%s : None(0)\n", status);
		return;
	}

	se = &task->se;
	if (task == curr) {
		seq_buf_printf(md_runq_seq_buf,
			       "[status: curr] pid: %d preempt: %#x\n",
			       task_pid_nr(task),
			       task->thread_info.preempt_count);
		return;
	}

	seq_buf_printf(md_runq_seq_buf,
		       "[status: %s] pid: %d\n",
		       status, task_pid_nr(task));
}

static void md_dump_cfs_rq(struct cfs_rq *cfs, struct task_struct *curr);

static void md_dump_cgroup_state(char *status, struct sched_entity *se_p,
				 struct task_struct *curr)
{
	struct task_struct *task;
	struct cfs_rq *my_q = NULL;
	unsigned int nr_running;

	if (!se_p) {
		md_dump_task_info(NULL, status, NULL);
		return;
	}
#ifdef CONFIG_FAIR_GROUP_SCHED
	my_q = se_p->my_q;
#endif
	if (!my_q) {
		task = container_of(se_p, struct task_struct, se);
		md_dump_task_info(task, status, curr);
		return;
	}
	nr_running = my_q->nr_running;
	md_dump_align();
	seq_buf_printf(md_runq_seq_buf, "%s: %d process is grouping\n",
				   status, nr_running);
	md_align_offset++;
	md_dump_cfs_rq(my_q, curr);
	md_align_offset--;
}

static void md_dump_cfs_node_func(struct rb_node *node,
				  struct task_struct *curr)
{
	struct sched_entity *se_p = container_of(node, struct sched_entity,
						 run_node);

	md_dump_cgroup_state("pend", se_p, curr);
}

static void md_rb_walk_cfs(struct rb_root_cached *rb_root_cached_p,
			   struct task_struct *curr)
{
	int max_walk = 200;	/* Bail out, in case of loop */
	struct rb_node *leftmost = rb_root_cached_p->rb_leftmost;
	struct rb_root *root = &rb_root_cached_p->rb_root;
	struct rb_node *rb_node = rb_first(root);

	if (!leftmost)
		return;
	while (rb_node && max_walk--) {
		md_dump_cfs_node_func(rb_node, curr);
		rb_node = rb_next(rb_node);
	}
}

static void md_dump_cfs_rq(struct cfs_rq *cfs, struct task_struct *curr)
{
	struct rb_root_cached *rb_root_cached_p = &cfs->tasks_timeline;

	md_dump_cgroup_state("curr", cfs->curr, curr);
	md_dump_cgroup_state("next", cfs->next, curr);
	md_rb_walk_cfs(rb_root_cached_p, curr);
}

static const char * const task_state_array[] = {
	"R", /* 0x00 */
	"S", /* 0x01 */
	"D", /* 0x02 */
	"T", /* 0x04 */
	"t", /* 0x08 */
	"X", /* 0x10 */
	"Z", /* 0x20 */
	"P", /* 0x40 */
	"I", /* 0x80 */
};

/* In line with task_state_index from fs/proc/array.c */
static inline unsigned int md_task_state_index(struct task_struct *tsk)
{
	unsigned int tsk_state = READ_ONCE(tsk->__state);
	unsigned int state = (tsk_state | tsk->exit_state) & TASK_REPORT;

	if (tsk_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;

	return fls(state);
}

/* In line with get_task_state from fs/proc/array.c */
static inline const char *md_get_task_state(struct task_struct *tsk)
{
	return task_state_array[md_task_state_index(tsk)];
}

static void md_dump_runqueues(void)
{
	struct task_struct *p, *t;

	if (!md_runq_seq_buf)
		return;

	seq_buf_printf(md_runq_seq_buf, "%-15s", "Task name");
	seq_buf_printf(md_runq_seq_buf, "%*s", 6, "PID");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Exec_started_at");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Last_queued_at");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Total_wait_time");
	seq_buf_printf(md_runq_seq_buf, "%*s", 12, "Exec_times");
	seq_buf_printf(md_runq_seq_buf, "%*s", 4, "CPU");
	seq_buf_printf(md_runq_seq_buf, "%*s", 5, "Prio");
	seq_buf_printf(md_runq_seq_buf, "%*s", 6, "State");
	seq_buf_printf(md_runq_seq_buf, "\n");

	for_each_process_thread(p, t) {
		seq_buf_printf(md_runq_seq_buf, "%-15s", t->comm);
		seq_buf_printf(md_runq_seq_buf, "%6d", t->pid);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.last_arrival);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.last_queued);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.run_delay);
		seq_buf_printf(md_runq_seq_buf, "%12ld", t->sched_info.pcount);
		seq_buf_printf(md_runq_seq_buf, "%4d", t->on_cpu);
		seq_buf_printf(md_runq_seq_buf, "%5d", t->prio);
		seq_buf_printf(md_runq_seq_buf, "%*s", 6, md_get_task_state(t));
		seq_buf_printf(md_runq_seq_buf, "\n");
	}
}

/*
 * dump a block of kernel memory from around the given address.
 * Bulk of the code is lifted from arch/arm64/kernel/proccess.c.
 */
static void md_dump_data(unsigned long addr, int nbytes, const char *name)
{
	int	i, j;
	int	nlines;
	u32	*p;

	/*
	 * don't attempt to dump non-kernel addresses or
	 * values that are probably just small negative numbers
	 */
	if (addr < PAGE_OFFSET || addr > -256UL)
		return;

	seq_buf_printf(md_cntxt_seq_buf, "\n%s: %#lx:\n", name, addr);

	/*
	 * round address down to a 32 bit boundary
	 * and always dump a multiple of 32 bytes
	 */
	p = (u32 *)(addr & ~(sizeof(u32) - 1));
	nbytes += (addr & (sizeof(u32) - 1));
	nlines = (nbytes + 31) / 32;

	for (i = 0; i < nlines; i++) {
		/*
		 * just display low 16 bits of address to keep
		 * each line of the dump < 80 characters
		 */
		seq_buf_printf(md_cntxt_seq_buf, "%04lx ",
			       (unsigned long)p & 0xffff);
		for (j = 0; j < 8; j++) {
			u32	data = 0;

			if (get_kernel_nofault(data, p))
				seq_buf_printf(md_cntxt_seq_buf, " ********");
			else
				seq_buf_printf(md_cntxt_seq_buf, " %08x", data);
			++p;
		}
		seq_buf_printf(md_cntxt_seq_buf, "\n");
	}
}

static void md_reg_context_data(struct pt_regs *regs)
{
	unsigned int i;
	int nbytes = 128;

	if (user_mode(regs) ||  !regs->pc)
		return;

	md_dump_data(regs->pc - nbytes, nbytes * 2, "PC");
	md_dump_data(regs->regs[30] - nbytes, nbytes * 2, "LR");
	md_dump_data(regs->sp - nbytes, nbytes * 2, "SP");
	for (i = 0; i < 30; i++) {
		char name[4];

		snprintf(name, sizeof(name), "X%u", i);
		md_dump_data(regs->regs[i] - nbytes, nbytes * 2, name);
	}
}

static inline void md_dump_panic_regs(void)
{
	struct pt_regs regs;
	u64 tmp1, tmp2;

	/* Lifted from crash_setup_regs() */
	__asm__ __volatile__ (
		"stp	 x0,   x1, [%2, #16 *  0]\n"
		"stp	 x2,   x3, [%2, #16 *  1]\n"
		"stp	 x4,   x5, [%2, #16 *  2]\n"
		"stp	 x6,   x7, [%2, #16 *  3]\n"
		"stp	 x8,   x9, [%2, #16 *  4]\n"
		"stp	x10,  x11, [%2, #16 *  5]\n"
		"stp	x12,  x13, [%2, #16 *  6]\n"
		"stp	x14,  x15, [%2, #16 *  7]\n"
		"stp	x16,  x17, [%2, #16 *  8]\n"
		"stp	x18,  x19, [%2, #16 *  9]\n"
		"stp	x20,  x21, [%2, #16 * 10]\n"
		"stp	x22,  x23, [%2, #16 * 11]\n"
		"stp	x24,  x25, [%2, #16 * 12]\n"
		"stp	x26,  x27, [%2, #16 * 13]\n"
		"stp	x28,  x29, [%2, #16 * 14]\n"
		"mov	 %0,  sp\n"
		"stp	x30,  %0,  [%2, #16 * 15]\n"

		"/* faked current PSTATE */\n"
		"mrs	 %0, CurrentEL\n"
		"mrs	 %1, SPSEL\n"
		"orr	 %0, %0, %1\n"
		"mrs	 %1, DAIF\n"
		"orr	 %0, %0, %1\n"
		"mrs	 %1, NZCV\n"
		"orr	 %0, %0, %1\n"
		/* pc */
		"adr	 %1, 1f\n"
		"1:\n"
		"stp	 %1, %0,   [%2, #16 * 16]\n"
		: "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&regs)
		: "memory"
		);

	seq_buf_printf(md_cntxt_seq_buf, "PANIC CPU : %d\n",
				   raw_smp_processor_id());
	md_reg_context_data(&regs);
}

static int md_die_context_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;

	if (md_in_oops_handler)
		return NOTIFY_DONE;
	md_in_oops_handler = true;
	if (!md_cntxt_seq_buf) {
		md_in_oops_handler = false;
		return NOTIFY_DONE;
	}
	die_cpu = raw_smp_processor_id();
	seq_buf_printf(md_cntxt_seq_buf, "\nDIE CPU : %d\n", die_cpu);
	md_reg_context_data(args->regs);
	md_in_oops_handler = false;
	return NOTIFY_DONE;
}

static struct notifier_block md_die_context_nb = {
	.notifier_call = md_die_context_notify,
	.priority = INT_MAX - 2, /* < msm watchdog die notifier */
};

static void show_val_kb(struct seq_buf *m, const char *s, unsigned long num)
{
	seq_buf_printf(m, "%s : %lld KB\n", s, num << (PAGE_SHIFT - 10));
}

void md_dump_meminfo(struct seq_buf *m)
{
	struct sysinfo i;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	int lru;

	si_meminfo(&i);

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
}

void md_dump_process(void)
{
	if (md_in_oops_handler)
		return;
	if (!atomic_add_unless(&md_handle_done, 1, 1))
		return;
	md_in_oops_handler = true;
	if (!md_cntxt_seq_buf)
		goto dump_rq;
	if (raw_smp_processor_id() != die_cpu)
		md_dump_panic_regs();
dump_rq:
	md_dump_runqueues();
	if (md_meminfo_seq_buf)
		md_dump_meminfo(md_meminfo_seq_buf);

	md_in_oops_handler = false;
}
EXPORT_SYMBOL(md_dump_process);

static int md_panic_handler(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	md_dump_process();
	return NOTIFY_DONE;
}

static struct notifier_block md_panic_blk = {
	.notifier_call = md_panic_handler,
	.priority = INT_MAX - 2, /* < msm watchdog panic notifier */
};

static int md_register_minidump_entry(char *name, u64 virt_addr,
				      u64 phys_addr, u64 size)
{
	struct md_region md_entry;
	int ret;

	strscpy(md_entry.name, name, sizeof(md_entry.name));
	md_entry.virt_addr = virt_addr;
	md_entry.phys_addr = phys_addr;
	md_entry.size = size;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0)
		pr_err("Failed to add %s entry in Minidump\n", name);
	return ret;
}

static int md_register_panic_entries(int num_pages, char *name,
				      struct seq_buf **global_buf)
{
	char *buf;
	struct seq_buf *seq_buf_p;
	int ret;

	buf = kzalloc(num_pages * PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -EINVAL;

	seq_buf_p = kzalloc(sizeof(*seq_buf_p), GFP_KERNEL);
	if (!seq_buf_p) {
		ret = -EINVAL;
		goto err_seq_buf;
	}

	ret = md_register_minidump_entry(name, (uintptr_t)buf,
					 virt_to_phys(buf),
					 num_pages * PAGE_SIZE);
	if (ret < 0)
		goto err_entry_reg;

	seq_buf_init(seq_buf_p, buf, num_pages * PAGE_SIZE);

	/* Complete registration before populating data */
	smp_mb();
	WRITE_ONCE(*global_buf, seq_buf_p);
	return 0;

err_entry_reg:
	kfree(seq_buf_p);
err_seq_buf:
	kfree(buf);
	return ret;
}

static void md_register_panic_data(void)
{
	md_register_panic_entries(MD_RUNQUEUE_PAGES, "KRUNQUEUE",
				  &md_runq_seq_buf);
	md_register_panic_entries(MD_CPU_CNTXT_PAGES, "KCNTXT",
				  &md_cntxt_seq_buf);
	md_register_panic_entries(MD_MEMINFO_PAGES, "MEMINFO",
				  &md_meminfo_seq_buf);
}

static int register_vmap_mem(const char *name, void *virual_addr, size_t dump_len)
{
	int to_dump;
	u64 phys_addr;
	char entry_name[12];
	void *dump_addr = virual_addr;
	int i = 0;

	while (dump_len) {
		to_dump = min(dump_len, PAGE_SIZE - offset_in_page(dump_addr));
		phys_addr = page_to_phys(vmalloc_to_page((const void *)dump_addr));
		snprintf(entry_name, sizeof(entry_name), "%d_%s", i, name);
		md_register_minidump_entry(entry_name, (u64)dump_addr, phys_addr, to_dump);
		dump_addr += to_dump;
		dump_len -= to_dump;
		i++;
	}

	return 0;
}

struct module_sect_attr {
	struct bin_attribute battr;
	unsigned long address;
};

struct module_sect_attrs {
	struct attribute_group grp;
	unsigned int nsections;
	struct module_sect_attr attrs[];
};

static int md_module_process(struct module *mod)
{
	int i;
	bool is_key_module = false;
	unsigned long sec_addr, base_addr;
	unsigned long dump_start, dump_end;

	for (i = 0; i < n_modump; i++) {
		if (strcmp(key_modules[i], mod->name) == 0)
			is_key_module = true;
	}

	if (md_mod_info_seq_buf) {
		base_addr = (unsigned long)mod->mem[MOD_TEXT].base;
		seq_buf_printf(md_mod_info_seq_buf, "name: %s, base: %lx",
				mod->name, base_addr);
		if (is_key_module) {
			dump_start = base_addr +
					mod->mem[MOD_RO_AFTER_INIT].size;
			dump_end = base_addr + mod->mem[MOD_TEXT].size;
			if (((dump_end - dump_start) / PAGE_SIZE) <
				msm_minidump_get_available_region()) {
				for (i = 0; i < mod->sect_attrs->nsections ; i++) {
					sec_addr = mod->sect_attrs->attrs[i].address;
					if (sec_addr >= dump_start && sec_addr < dump_end) {
						seq_buf_printf(md_mod_info_seq_buf, ", %s: %lx",
							mod->sect_attrs->attrs[i].battr.attr.name,
									sec_addr);
					}
				}
				register_vmap_mem(mod->name, (void *)dump_start,
						(dump_end - dump_start));
			} else
				pr_err("Failed to dump module %s\n", mod->name);
		}
		seq_buf_printf(md_mod_info_seq_buf, "\n");
	}

	return 0;
}

static int md_module_notify(struct notifier_block *self,
			    unsigned long val, void *data)
{
	struct module *mod = data;

	spin_lock(&md_modules_lock);
	if (mod->state == MODULE_STATE_LIVE)
		md_module_process(mod);
	spin_unlock(&md_modules_lock);
	return 0;
}

static struct notifier_block md_module_nb = {
	.notifier_call = md_module_notify,
};

static void md_register_module_data(void)
{
	int ret;

	ret = md_register_panic_entries(MD_MODULE_PAGES, "KMODULES",
					&md_mod_info_seq_buf);
	if (ret) {
		pr_err("Failed to register minidump module buffer\n");
		return;
	}

	seq_buf_printf(md_mod_info_seq_buf, "=== MODULE INFO ===\n");
	ret = register_module_notifier(&md_module_nb);
	if (ret) {
		pr_err("Failed to register minidump module notifier\n");
		return;
	}

}

static void register_pstore_info(void)
{
	int ret;
	struct device_node *node;
	struct resource resource;
	struct reserved_mem *rmem = NULL;
	unsigned int size;
	phys_addr_t paddr;
	unsigned long total_size;
	struct md_region md_entry;

	node = of_find_compatible_node(NULL, NULL, "ramoops");
	if (IS_ERR_OR_NULL(node)) {
		pr_err("Failed to get pstore node\n");
		return;
	}

	ret = of_address_to_resource(node, 0, &resource);
	if (ret) {
		rmem = of_reserved_mem_lookup(node);
		if (rmem) {
			paddr = rmem->base;
			total_size = rmem->size;
		} else {
			pr_err("Failed to get pstore mem\n");
			return;
		}
	} else {
		paddr = resource.start;
		total_size = resource_size(&resource);
	}

	ret = of_property_read_u32(node, "record-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KDMESG", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add dmesg in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "console-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KCONSOLE", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add console in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "ftrace-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KFTRACE", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add ftrace in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "pmsg-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KPMSG", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add pmsg in Minidump\n");

		paddr += size;
	}
}

static void md_kmsg_dump(struct kmsg_dumper *dumper,
			enum kmsg_dump_reason reason)
{
	struct kmsg_dump_iter iter;
	const char      *why;
	char *dst = kmsg_buf;
	size_t dst_size = kmsg_dump_sz;
	int header_size;

	why = kmsg_dump_reason_str(reason);

	kmsg_dump_rewind(&iter);

	/* Write dump header. */
	header_size = snprintf(dst, dst_size, "MiniDump: %s\n", why);
	dst_size -= header_size;

	if (!kmsg_dump_get_buffer(&iter, true, dst + header_size,
				  dst_size, NULL))
		pr_crit("No Dump received\n");
}

static struct kmsg_dumper md_dumper = {
	.dump = md_kmsg_dump,
};


static int md_kmsg_dump_register(void)
{
	int ret;
	struct md_region md_entry;

	ret = kmsg_dump_register(&md_dumper);
	if (ret < 0)
		return ret;

        strscpy(md_entry.name, "KMSG", sizeof(md_entry.name));
        md_entry.virt_addr = (uintptr_t)kmsg_buf;
        md_entry.phys_addr = virt_to_phys(kmsg_buf);
        md_entry.size = kmsg_dump_sz;

        ret = msm_minidump_add_region(&md_entry);
	if (ret < 0)
		kmsg_dump_unregister(&md_dumper);
	return ret;
}

int msm_minidump_log_init(void)
{
	int ret;

	kmsg_buf = kzalloc(kmsg_dump_sz, GFP_KERNEL);

	if (!kmsg_buf)
		return -ENOMEM;

	ret = md_kmsg_dump_register();
	if (ret < 0)
		return ret;

	is_vmap_stack = IS_ENABLED(CONFIG_VMAP_STACK);
	register_suspend_context();
	register_pstore_info();
	md_register_trace_buf();
	md_register_module_data();
	md_register_panic_data();
	atomic_notifier_chain_register(&panic_notifier_list, &md_panic_blk);
	register_die_notifier(&md_die_context_nb);
	return 0;
}

module_param(kmsg_dump_sz, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(initial_descriptor_timeout,
                "initial 64-byte descriptor request timeout in milliseconds "
                "(default 5000 - 5.0 seconds)");
