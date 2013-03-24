/*
 * Claudiu Ghioc 341 C1
 * KTracer - Kprobe based tracer
 */

#include "ktracer.h"


struct handler_data {
	long size;
};

/* Increase the number of hits for a specific function */
static void inc_counter(int pid, int func_index)
{
	struct proc_info *p_info;
	struct hlist_node *i;

	hash_for_each_possible(procs, p_info, i, hlh, pid) {
		if (p_info->pid != pid)
			continue;
		atomic64_inc(&p_info->results[func_index]);
		break;
	}
}

/* Add to the number of allocated/freed memory */
static void add_counter(int pid, int func_index, long quant)
{
	struct proc_info *p_info;
	struct hlist_node *i;

	hash_for_each_possible(procs, p_info, i, hlh, pid) {
		if (p_info->pid != pid)
			continue;
		atomic64_add(quant, &p_info->results[func_index]);
		break;
	}
}

/* Save the association between address - size in process info */
static void save_mm_info(long address, long size)
{
	struct mem_data *mm_data;
	struct proc_info *p_info;
	struct hlist_node *i;

	/* Build the association */
	mm_data = kmalloc(sizeof(*mm_data), GFP_KERNEL);
	if (mm_data == NULL)
		return;
	mm_data->address = address;
	mm_data->size = size;

	/* Find the process info and add the association */
	hash_for_each_possible(procs, p_info, i, hlh, current->pid) {
		if (p_info->pid != current->pid)
			continue;
		list_add(&mm_data->lh, &p_info->mm);
		break;
	}
}

/* kmalloc handler */
static int kmalloc_h(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct handler_data *h_data;
	long retval;

	/* Get the saved size */
	h_data = (struct handler_data *)ri->data;
	retval = regs_return_value(regs);
	printk(LOG_LEVEL "kmalloc handler %d, size %ld, ret %lx\n", current->pid, h_data->size, retval);

	/* Save the association address - size */
	save_mm_info(retval, h_data->size);
	return 0;
}

/* kmalloc entry handler */
static int kmalloc_eh(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct handler_data *h_data;

	/* Add to hit count and memory size */
	inc_counter(current->pid, KMALLOC_INDEX);
	add_counter(current->pid, KMALLOC_MEM_INDEX, regs->ax);
	printk(LOG_LEVEL "kmalloc entry %d, size %ld\n", current->pid, regs->ax);

	/* Save the size to allocate */
	h_data = (struct handler_data *)ri->data;
	h_data->size = regs->ax;

	return 0;
}

/* kfree handler */
static int kfree_h(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

/* kfree entry handler */
static int kfree_eh(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	inc_counter(current->pid, KFREE_INDEX);
	return 0;
}

/* schedule entry handler */
asmlinkage void __sched schedule_en(void)
{
	inc_counter(current->pid, SCHEDULE_INDEX);
	jprobe_return();
}

/* up entry handler */
static void up_en(struct semaphore *sem)
{
	inc_counter(current->pid, UP_INDEX);
	jprobe_return();
}

/* down interruptible entry handler */
static int down_en(struct semaphore *sem)
{
	inc_counter(current->pid, DOWN_INT_INDEX);
	jprobe_return();
	return 0;
}

/* mutex_lock entry handler */
static void __sched mutex_lock_en(struct mutex *lock)
{
	inc_counter(current->pid, MUTEX_LCK_INDEX);
	jprobe_return();
}

/* mutex_unlock entry handler */
static void __sched mutex_unlock_en(struct mutex *lock)
{
	inc_counter(current->pid, MUTEX_ULK_INDEX);
	jprobe_return();
}

/* Kretprobes for kmalloc and kfree */
struct kretprobe **mem_probes = (struct kretprobe *[]){

	/* Kretprobe for kmalloc */
	& (struct kretprobe) {
		.kp = {
			.symbol_name = "__kmalloc"
		},
		.entry_handler	= kmalloc_eh,
		.handler	= kmalloc_h,
		.maxactive	= NR_CPUS,
		.data_size	= sizeof(struct handler_data)
	},

	/* Kretprobe for kfree */
	& (struct kretprobe) {
		.kp = {
			.symbol_name = "kfree"
		},
		.entry_handler	= kfree_eh,
		.handler	= kfree_h,
		.maxactive	= NR_CPUS,
		.data_size	= sizeof(struct handler_data)
	}
};

/* Jprobes for the rest of the functions needed */
struct jprobe **func_probes = (struct jprobe *[]) {

	/* Jprobe for schedule */
	& (struct jprobe) {
		.kp = {
			.symbol_name = "schedule"
		},
		.entry = schedule_en
	},

	/* Jprobe for up */
	& (struct jprobe) {
		.kp = {
			.symbol_name = "up"
		},
		.entry = up_en
	},

	/* Jprobe for down_interruptible*/
	& (struct jprobe) {
		.kp = {
			.symbol_name = "down_interruptible"
		},
		.entry = down_en
	},

	/* Jprobe for mutex_lock */
	& (struct jprobe) {
		.kp = {
			.symbol_name = "mutex_lock_nested"
		},
		.entry = mutex_lock_en
	},

	/* Jprobe for mutex_unlock */
	& (struct jprobe) {
		.kp = {
			.symbol_name = "mutex_unlock"
		},
		.entry = mutex_unlock_en
	}
};
