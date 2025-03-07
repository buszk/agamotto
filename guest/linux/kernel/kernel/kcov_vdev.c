// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "kcov: " fmt

#define DISABLE_BRANCH_PROFILING
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kcov.h>
#include <linux/refcount.h>
#include <asm/setup.h>

/* Number of 64-bit words written per one comparison: */
#define KCOV_WORDS_PER_CMP 4

/*
 * kcov descriptor (one per opened debugfs file).
 * State transitions of the descriptor:
 *  - initial state after open()
 *  - then there must be a single ioctl(KCOV_INIT_TRACE) call
 *  - then, mmap() call (several calls are allowed but not useful)
 *  - then, ioctl(KCOV_ENABLE, arg), where arg is
 *	KCOV_TRACE_PC - to trace only the PCs
 *	or
 *	KCOV_TRACE_CMP - to trace only the comparison operands
 *  - then, ioctl(KCOV_DISABLE) to disable the task.
 * Enabling/disabling ioctls can be repeated (only one task a time allowed).
 */
struct kcov {
	/*
	 * Reference counter. We keep one for:
	 *  - opened file descriptor
	 *  - task with enabled coverage (we can't unwire it from another task)
	 *  - each code section for remote coverage collection
	 */
	refcount_t		refcount;
	/* The lock protects mode, size, area and t. */
	spinlock_t		lock;
	enum kcov_mode		mode;
	/* Size of arena (in long's). */
	unsigned int		size;
	/* Coverage buffer shared with user space. */
	void			*area;
	/* Task for which we collect coverage, or NULL. */
	struct task_struct	*t;
	/* Collecting coverage from remote threads. */
	bool			remote;
	/* Size of remote arena (in long's). */
	unsigned int		remote_size;
	/* Sequence is incremented each time kcov is reenabled. */
	int			sequence;

	union {
		/* For KCOV_MODE_TRACE_AFL */
		struct {
			/* (number of bytes) - 1, where number of
			* bytes must be a power of 2. */
			unsigned int            mask;
			/* Previous PC (for KCOV_MODE_TRACE_AFL). */
			unsigned int            prev_ip;
		};
	};
};

#ifdef CONFIG_KCOV_VDEV
static struct task_struct task_g;
static struct kcov *kcov_g;
static void (*kcov_enable_callback)(void *, unsigned int, bool);
static void (*kcov_disable_callback)(void *, bool);
static void (*kcov_flush_callback)(void *, unsigned int);

uint64_t kcov_get_area_offset_g(void)
{
	if (!task_g.kcov)
		return -EINVAL;

	if (task_g.kcov_area) {
		uint64_t phys = virt_to_phys(task_g.kcov_area);
		return phys;
	}

	return 0;
}
#endif

struct kcov_remote_area {
	struct list_head	list;
	unsigned int		size;
};

struct kcov_remote {
	u64			handle;
	struct kcov		*kcov;
	struct hlist_node	hnode;
};

static DEFINE_SPINLOCK(kcov_remote_lock);
static DEFINE_HASHTABLE(kcov_remote_map, 4);
static struct list_head kcov_remote_areas = LIST_HEAD_INIT(kcov_remote_areas);

static struct kcov_remote *kcov_remote_find(u64 handle)
{
	struct kcov_remote *remote;

	hash_for_each_possible(kcov_remote_map, remote, hnode, handle) {
		if (remote->handle == handle)
			return remote;
	}
	return NULL;
}

static struct kcov_remote *kcov_remote_add(struct kcov *kcov, u64 handle)
{
	struct kcov_remote *remote;

	if (kcov_remote_find(handle))
		return ERR_PTR(-EEXIST);
	remote = kmalloc(sizeof(*remote), GFP_ATOMIC);
	if (!remote)
		return ERR_PTR(-ENOMEM);
	remote->handle = handle;
	remote->kcov = kcov;
	hash_add(kcov_remote_map, &remote->hnode, handle);
	return remote;
}

static struct kcov_remote_area *kcov_remote_area_get(unsigned int size)
{
	struct kcov_remote_area *area;
	struct list_head *pos;

	list_for_each(pos, &kcov_remote_areas) {
		area = list_entry(pos, struct kcov_remote_area, list);
		if (area->size == size) {
			list_del(&area->list);
			return area;
		}
	}
	return NULL;
}

static void kcov_remote_area_put(struct kcov_remote_area *area,
					unsigned int size)
{
	INIT_LIST_HEAD(&area->list);
	area->size = size;
	list_add(&area->list, &kcov_remote_areas);
}

static notrace bool check_kcov_mode(enum kcov_mode needed_mode, struct task_struct *t)
{
	unsigned int mode;

	/*
	 * We are interested in code coverage as a function of a syscall inputs,
	 * so we ignore code executed in interrupts.
	 */
	if (!in_task())
		return false;
	mode = READ_ONCE(t->kcov_mode);
	/*
	 * There is some code that runs in interrupts but for which
	 * in_interrupt() returns false (e.g. preempt_schedule_irq()).
	 * READ_ONCE()/barrier() effectively provides load-acquire wrt
	 * interrupts, there are paired barrier()/WRITE_ONCE() in
	 * kcov_start().
	 */
	barrier();
	return mode == needed_mode;
}

static notrace unsigned long canonicalize_ip(unsigned long ip)
{
#ifdef CONFIG_RANDOMIZE_BASE
	ip -= kaslr_offset();
#endif
	return ip;
}

/*
 * Entry point from instrumented code.
 * This is called once per basic-block/edge.
 */
void notrace __sanitizer_cov_trace_pc(void)
{
	struct task_struct *t;
	unsigned long *area;
	unsigned long ip = canonicalize_ip(_RET_IP_);
	unsigned long pos;

	t = current;

#ifdef CONFIG_KCOV_VDEV
	if (task_g.kcov_area) {
		t = &task_g;
	}
#endif

	if (check_kcov_mode(KCOV_MODE_TRACE_AFL, t)) {
		area = t->kcov_area;
		unsigned char *area_bytes = (unsigned char *)area;
		++area_bytes[(t->prev_ip ^ ip) & t->kcov->mask];
		t->prev_ip = hash_long(ip, BITS_PER_LONG);
		// no need to flush
		return;
	}

	if (!check_kcov_mode(KCOV_MODE_TRACE_PC, t))
		return;

	area = t->kcov_area;
	/* The first 64-bit word is the number of subsequent PCs. */
	pos = READ_ONCE(area[0]) + 1;
#ifdef CONFIG_KCOV_VDEV
	if (kcov_flush_callback && unlikely(pos == t->kcov_size)) {
		kcov_flush_callback(area, t->kcov_size);
	}
#endif
	if (likely(pos < t->kcov_size)) {
		area[pos] = ip;
		WRITE_ONCE(area[0], pos);
	}
}
EXPORT_SYMBOL(__sanitizer_cov_trace_pc);

#ifdef CONFIG_KCOV_ENABLE_COMPARISONS
static void notrace write_comp_data(u64 type, u64 arg1, u64 arg2, u64 ip)
{
	struct task_struct *t;
	u64 *area;
	u64 count, start_index, end_pos, max_pos;

	t = current;
	if (!check_kcov_mode(KCOV_MODE_TRACE_CMP, t))
		return;

	ip = canonicalize_ip(ip);

	/*
	 * We write all comparison arguments and types as u64.
	 * The buffer was allocated for t->kcov_size unsigned longs.
	 */
	area = (u64 *)t->kcov_area;
	max_pos = t->kcov_size * sizeof(unsigned long);

	count = READ_ONCE(area[0]);

	/* Every record is KCOV_WORDS_PER_CMP 64-bit words. */
	start_index = 1 + count * KCOV_WORDS_PER_CMP;
	end_pos = (start_index + KCOV_WORDS_PER_CMP) * sizeof(u64);
	if (likely(end_pos <= max_pos)) {
		area[start_index] = type;
		area[start_index + 1] = arg1;
		area[start_index + 2] = arg2;
		area[start_index + 3] = ip;
		WRITE_ONCE(area[0], count + 1);
	}
}

void notrace __sanitizer_cov_trace_cmp1(u8 arg1, u8 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(0), arg1, arg2, _RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_cmp1);

void notrace __sanitizer_cov_trace_cmp2(u16 arg1, u16 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(1), arg1, arg2, _RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_cmp2);

void notrace __sanitizer_cov_trace_cmp4(u32 arg1, u32 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(2), arg1, arg2, _RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_cmp4);

void notrace __sanitizer_cov_trace_cmp8(u64 arg1, u64 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(3), arg1, arg2, _RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_cmp8);

void notrace __sanitizer_cov_trace_const_cmp1(u8 arg1, u8 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(0) | KCOV_CMP_CONST, arg1, arg2,
			_RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_const_cmp1);

void notrace __sanitizer_cov_trace_const_cmp2(u16 arg1, u16 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(1) | KCOV_CMP_CONST, arg1, arg2,
			_RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_const_cmp2);

void notrace __sanitizer_cov_trace_const_cmp4(u32 arg1, u32 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(2) | KCOV_CMP_CONST, arg1, arg2,
			_RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_const_cmp4);

void notrace __sanitizer_cov_trace_const_cmp8(u64 arg1, u64 arg2)
{
	write_comp_data(KCOV_CMP_SIZE(3) | KCOV_CMP_CONST, arg1, arg2,
			_RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_const_cmp8);

void notrace __sanitizer_cov_trace_switch(u64 val, u64 *cases)
{
	u64 i;
	u64 count = cases[0];
	u64 size = cases[1];
	u64 type = KCOV_CMP_CONST;

	switch (size) {
	case 8:
		type |= KCOV_CMP_SIZE(0);
		break;
	case 16:
		type |= KCOV_CMP_SIZE(1);
		break;
	case 32:
		type |= KCOV_CMP_SIZE(2);
		break;
	case 64:
		type |= KCOV_CMP_SIZE(3);
		break;
	default:
		return;
	}
	for (i = 0; i < count; i++)
		write_comp_data(type, cases[i + 2], val, _RET_IP_);
}
EXPORT_SYMBOL(__sanitizer_cov_trace_switch);
#endif /* ifdef CONFIG_KCOV_ENABLE_COMPARISONS */

static void kcov_start(struct task_struct *t, unsigned int size,
			void *area, enum kcov_mode mode, int sequence)
{
	/* Cache in task struct for performance. */
	t->kcov_size = size;
	t->kcov_area = area;
	/* See comment in check_kcov_mode(). */
	barrier();
	WRITE_ONCE(t->kcov_mode, mode);
	t->kcov_sequence = sequence;
}

static void kcov_stop(struct task_struct *t)
{
	WRITE_ONCE(t->kcov_mode, KCOV_MODE_DISABLED);
	barrier();
	t->kcov_size = 0;
	t->kcov_area = NULL;
}

static void kcov_task_reset(struct task_struct *t)
{
	kcov_stop(t);
	t->kcov = NULL;
	t->prev_ip = 0;
	t->kcov_sequence = 0;
	t->kcov_handle = 0;
}

void kcov_task_init(struct task_struct *t)
{
	kcov_task_reset(t);
	t->kcov_handle = current->kcov_handle;
}

static void kcov_reset(struct kcov *kcov)
{
	kcov->t = NULL;
	kcov->mode = KCOV_MODE_INIT;
	kcov->remote = false;
	kcov->remote_size = 0;
	kcov->sequence++;
}

static void kcov_remote_reset(struct kcov *kcov)
{
	int bkt;
	struct kcov_remote *remote;
	struct hlist_node *tmp;

	spin_lock(&kcov_remote_lock);
	hash_for_each_safe(kcov_remote_map, bkt, tmp, remote, hnode) {
		if (remote->kcov != kcov)
			continue;
		hash_del(&remote->hnode);
		kfree(remote);
	}
	/* Do reset before unlock to prevent races with kcov_remote_start(). */
	kcov_reset(kcov);
	spin_unlock(&kcov_remote_lock);
}

static void kcov_get(struct kcov *kcov)
{
	refcount_inc(&kcov->refcount);
}

static void kcov_put(struct kcov *kcov)
{
	if (refcount_dec_and_test(&kcov->refcount)) {
		kcov_remote_reset(kcov);
		vfree(kcov->area);
		kfree(kcov);
	}
}

void kcov_task_exit(struct task_struct *t)
{
	struct kcov *kcov;

	kcov = t->kcov;
	if (kcov == NULL)
		return;

	spin_lock(&kcov->lock);
	/*
	 * If !kcov->remote, this checks that t->kcov->t == t.
	 * If kcov->remote == true then the exiting task is either:
	 * 1. a remote task between kcov_remote_start() and kcov_remote_stop(),
	 *    in this case t != kcov->t and we'll print a warning; or
	 * 2. the task that created kcov exiting without calling KCOV_DISABLE,
	 *    in this case t == kcov->t and no warning is printed.
	 */
	if (WARN_ON(kcov->t != t)) {
		spin_unlock(&kcov->lock);
		return;
	}
	/* Just to not leave dangling references behind. */
	kcov_task_reset(t);
	if (kcov->remote)
		kcov_remote_reset(kcov);
	else
		kcov_reset(kcov);
	spin_unlock(&kcov->lock);
	kcov_put(kcov);
}

static int kcov_mmap(struct file *filep, struct vm_area_struct *vma)
{
	int res = 0;
	void *area;
	struct kcov *kcov = vma->vm_file->private_data;
	unsigned long size, off;
	struct page *page;

	area = vmalloc_user(vma->vm_end - vma->vm_start);
	if (!area)
		return -ENOMEM;

	spin_lock(&kcov->lock);
	size = kcov->size * sizeof(unsigned long);
	if (kcov->mode != KCOV_MODE_INIT || vma->vm_pgoff != 0 ||
	    vma->vm_end - vma->vm_start != size) {
		res = -EINVAL;
		goto exit;
	}
	if (!kcov->area) {
		kcov->area = area;
		vma->vm_flags |= VM_DONTEXPAND;
		spin_unlock(&kcov->lock);
		for (off = 0; off < size; off += PAGE_SIZE) {
			page = vmalloc_to_page(kcov->area + off);
			if (vm_insert_page(vma, vma->vm_start + off, page))
				WARN_ONCE(1, "vm_insert_page() failed");
		}
		return 0;
	}
exit:
	spin_unlock(&kcov->lock);
	vfree(area);
	return res;
}

#ifdef CONFIG_KCOV_VDEV
int kcov_mmap_g(void)
{
	int res = 0;
	void *area;
	struct kcov *kcov = kcov_g;
	unsigned long size;

	if (!kcov)
		return -EINVAL;

	spin_lock(&kcov->lock);
	size = kcov->size * sizeof(unsigned long);
	if (kcov->mode != KCOV_MODE_INIT) {
		res = -EINVAL;
		goto exit;
	}

	area = kmalloc(size, GFP_KERNEL|GFP_DMA);
	if (!area) {
		res = -ENOMEM;
		goto exit;
	}

	memset(area, 0x0, size);

	if (!kcov->area) {
		kcov->area = area;
		spin_unlock(&kcov->lock);
		return 0;
	}

	kfree(area);
exit:
	spin_unlock(&kcov->lock);
	return res;
}
#endif

static struct kcov *kcov_create(void)
{
	struct kcov *kcov;

	kcov = kzalloc(sizeof(*kcov), GFP_KERNEL);
	if (!kcov)
		return NULL;
	kcov->mode = KCOV_MODE_DISABLED;
	kcov->sequence = 1;
	refcount_set(&kcov->refcount, 1);
	spin_lock_init(&kcov->lock);

	return kcov;
}

#ifdef CONFIG_KCOV_VDEV
int kcov_open_g(void)
{
	kcov_g = kcov_create();
	if (!kcov_g)
		return -EFAULT;
	return 0;
}
#endif

static int kcov_open(struct inode *inode, struct file *filep)
{
	filep->private_data = kcov_create();
	return nonseekable_open(inode, filep);
}

static int kcov_close(struct inode *inode, struct file *filep)
{
	kcov_put(filep->private_data);
	return 0;
}

#ifdef CONFIG_KCOV_VDEV
int kcov_close_g(void) {
	if (kcov_g) {
		kcov_put(kcov_g);
	}
	return 0;
}

void kcov_register_enable_callback(void (*cb)(void *, unsigned int, bool)) {
	kcov_enable_callback = cb;
}

void kcov_register_disable_callback(void (*cb)(void *, bool)) {
	kcov_disable_callback = cb;
}

void kcov_register_flush_callback(void (*cb)(void *, unsigned int)) {
	kcov_flush_callback = cb;
}
#endif

static int kcov_get_mode(unsigned long arg)
{
	if (arg == KCOV_TRACE_PC)
		return KCOV_MODE_TRACE_PC;
	else if (arg == KCOV_TRACE_CMP)
#ifdef CONFIG_KCOV_ENABLE_COMPARISONS
		return KCOV_MODE_TRACE_CMP;
#else
		return -ENOTSUPP;
#endif
	else if (arg == KCOV_TRACE_AFL)
		return KCOV_MODE_TRACE_AFL;
	else
		return -EINVAL;
}

/*
 * Fault in a lazily-faulted vmalloc area before it can be used by
 * __santizer_cov_trace_pc(), to avoid recursion issues if any code on the
 * vmalloc fault handling path is instrumented.
 */
static void kcov_fault_in_area(struct kcov *kcov)
{
	unsigned long stride = PAGE_SIZE / sizeof(unsigned long);
	unsigned long *area = kcov->area;
	unsigned long offset;

	for (offset = 0; offset < kcov->size; offset += stride)
		READ_ONCE(area[offset]);
}

int kcov_ioctl_locked(struct kcov *kcov, unsigned int cmd,
			     unsigned long arg)
{
	struct task_struct *t;
	unsigned long size, unused;
	int mode, i;
	struct kcov_remote_arg *remote_arg;
	struct kcov_remote *remote;

	switch (cmd) {
	case KCOV_INIT_TRACE:
		/*
		 * Enable kcov in trace mode and setup buffer size.
		 * Must happen before anything else.
		 */
		if (kcov->mode != KCOV_MODE_DISABLED)
			return -EBUSY;
		/*
		 * Size must be at least 2 to hold current position and one PC.
		 * Later we allocate size * sizeof(unsigned long) memory,
		 * that must not overflow.
		 */
		size = arg;
		if (size < 2 || size > INT_MAX / sizeof(unsigned long))
			return -EINVAL;
		kcov->size = size;
		kcov->prev_ip = hash_long(0, BITS_PER_LONG);
		kcov->mask = (size * sizeof(unsigned long)) - 1;
		kcov->mode = KCOV_MODE_INIT;
		return 0;
	case KCOV_ENABLE:
		/*
		 * Enable coverage for the current task.
		 * At this point user must have been enabled trace mode,
		 * and mmapped the file. Coverage collection is disabled only
		 * at task exit or voluntary by KCOV_DISABLE. After that it can
		 * be enabled for another task.
		 */
		if (kcov->mode != KCOV_MODE_INIT || !kcov->area)
			return -EINVAL;
		t = current;
#ifdef CONFIG_KCOV_VDEV
		if (kcov_g == kcov)
			t = &task_g;
#endif
		if (kcov->t != NULL || t->kcov != NULL)
			return -EBUSY;
		mode = kcov_get_mode(arg);
		if (mode < 0)
			return mode;
		kcov_fault_in_area(kcov);
		kcov->mode = mode;
		kcov_start(t, kcov->size, kcov->area, kcov->mode,
				kcov->sequence);
		t->kcov = kcov;
		t->prev_ip = 0;
		kcov->t = t;
		/* Put either in kcov_task_exit() or in KCOV_DISABLE. */
		kcov_get(kcov);
#ifdef CONFIG_KCOV_VDEV
		if (kcov_enable_callback) {
			kcov_enable_callback(kcov->area, kcov->size, false);
		}
#endif
		return 0;
	case KCOV_DISABLE:
		/* Disable coverage for the current task. */
		unused = arg;
		t = current;
#ifdef CONFIG_KCOV_VDEV
		if (kcov_g == kcov)
			t = &task_g;
#endif
		if (unused != 0 || t->kcov != kcov)
			return -EINVAL;
		if (WARN_ON(kcov->t != t))
			return -EINVAL;
#ifdef CONFIG_KCOV_VDEV
		if (kcov_disable_callback && !kcov->remote) {
			kcov_disable_callback(kcov->area, kcov->remote ? true : false);
		}
#endif
		kcov_task_reset(t);
		if (kcov->remote)
			kcov_remote_reset(kcov);
		else
			kcov_reset(kcov);
		kcov_put(kcov);
		return 0;
	case KCOV_REMOTE_ENABLE:
		if (kcov->mode != KCOV_MODE_INIT || !kcov->area)
			return -EINVAL;
		t = current;
		if (kcov->t != NULL || t->kcov != NULL)
			return -EBUSY;
		remote_arg = (struct kcov_remote_arg *)arg;
		mode = kcov_get_mode(remote_arg->trace_mode);
		if (mode < 0)
			return mode;
		kcov->mode = mode;
		t->kcov = kcov;
		t->prev_ip = 0;
		kcov->t = t;
		kcov->remote = true;
		kcov->remote_size = remote_arg->area_size;
		spin_lock(&kcov_remote_lock);
		for (i = 0; i < remote_arg->num_handles; i++) {
			remote = kcov_remote_add(kcov, remote_arg->handles[i]);
			if (IS_ERR(remote)) {
				spin_unlock(&kcov_remote_lock);
				kcov_remote_reset(kcov);
				return PTR_ERR(remote);
			}
		}
		if (remote_arg->common_handle) {
			remote = kcov_remote_add(kcov,
					remote_arg->common_handle);
			if (IS_ERR(remote)) {
				spin_unlock(&kcov_remote_lock);
				kcov_remote_reset(kcov);
				return PTR_ERR(remote);
			}
			t->kcov_handle = remote_arg->common_handle;
		}
		spin_unlock(&kcov_remote_lock);
		/* Put either in kcov_task_exit() or in KCOV_DISABLE. */
		kcov_get(kcov);
		return 0;
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_KCOV_VDEV
int kcov_ioctl_locked_g(unsigned int cmd, unsigned long arg)
{
	if (kcov_g)
		return kcov_ioctl_locked(kcov_g, cmd, arg);
	return -1;
}
#endif

static long kcov_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct kcov *kcov;
	int res;
	struct kcov_remote_arg *remote_arg = NULL;
	unsigned int remote_num_handles;
	unsigned long remote_arg_size;

	if (cmd == KCOV_REMOTE_ENABLE) {
		if (get_user(remote_num_handles, (unsigned __user *)(arg +
				offsetof(struct kcov_remote_arg, num_handles))))
			return -EFAULT;
		if (remote_num_handles > KCOV_REMOTE_MAX_HANDLES)
			return -EINVAL;
		remote_arg_size = sizeof(*remote_arg) +
			remote_num_handles * sizeof(u64);
		remote_arg = memdup_user((void __user *)arg, remote_arg_size);
		if (IS_ERR(remote_arg))
			return PTR_ERR(remote_arg);
		if (remote_arg->num_handles != remote_num_handles) {
			kfree(remote_arg);
			return -EINVAL;
		}
		arg = (unsigned long)remote_arg;
	}

	kcov = filep->private_data;
	spin_lock(&kcov->lock);
	res = kcov_ioctl_locked(kcov, cmd, arg);
	spin_unlock(&kcov->lock);

	kfree(remote_arg);

	return res;
}

static const struct file_operations kcov_fops = {
	.open		= kcov_open,
	.unlocked_ioctl	= kcov_ioctl,
	.compat_ioctl	= kcov_ioctl,
	.mmap		= kcov_mmap,
	.release        = kcov_close,
};

void kcov_remote_start(u64 handle)
{
	struct kcov_remote *remote;
	void *area;
	struct task_struct *t;
	unsigned int size;
	enum kcov_mode mode;
	int sequence;

	if (WARN_ON(!in_task()))
		return;
	t = current;
	/*
	 * Check that kcov_remote_start is not called twice
	 * nor called by user tasks (with enabled kcov).
	 */
	if (WARN_ON(t->kcov))
		return;

	spin_lock(&kcov_remote_lock);
	remote = kcov_remote_find(handle);
	if (!remote) {
		spin_unlock(&kcov_remote_lock);
		return;
	}
	/* Put in kcov_remote_stop(). */
	kcov_get(remote->kcov);
	t->kcov = remote->kcov;
	t->prev_ip = 0;
	/*
	 * Read kcov fields before unlock to prevent races with
	 * KCOV_DISABLE / kcov_remote_reset().
	 */
	size = remote->kcov->remote_size;
	mode = remote->kcov->mode;
	sequence = remote->kcov->sequence;
	area = kcov_remote_area_get(size);
	spin_unlock(&kcov_remote_lock);

	if (!area) {
#ifdef CONFIG_KCOV_VDEV
		area = kmalloc(size * sizeof(unsigned long), GFP_KERNEL|GFP_DMA);
#else
		area = vmalloc(size * sizeof(unsigned long));
#endif
		if (!area) {
			kcov_put(remote->kcov);
			return;
		}
	}
	/* Reset coverage size. */
	*(u64 *)area = 0;

#ifdef CONFIG_KCOV_VDEV
	if (kcov_enable_callback) {
		kcov_enable_callback(area, size, true);
	}
#endif

	kcov_start(t, size, area, mode, sequence);
}

static void kcov_move_area(enum kcov_mode mode, void *dst_area,
				unsigned int dst_area_size, void *src_area)
{
	u64 word_size = sizeof(unsigned long);
	u64 count_size, entry_size;
	u64 dst_len, src_len;
	void *dst_entries, *src_entries;
	u64 dst_occupied, dst_free, bytes_to_move, entries_moved;

	switch (mode) {
	case KCOV_MODE_TRACE_PC:
	case KCOV_MODE_TRACE_AFL:
		dst_len = READ_ONCE(*(unsigned long *)dst_area);
		src_len = *(unsigned long *)src_area;
		count_size = sizeof(unsigned long);
		entry_size = sizeof(unsigned long);
		break;
	case KCOV_MODE_TRACE_CMP:
		dst_len = READ_ONCE(*(u64 *)dst_area);
		src_len = *(u64 *)src_area;
		count_size = sizeof(u64);
		entry_size = sizeof(u64) * KCOV_WORDS_PER_CMP;
		break;
	default:
		WARN_ON(1);
		return;
	}

	if (dst_len > (dst_area_size * word_size - count_size) / entry_size)
		return;
	dst_occupied = count_size + dst_len * entry_size;
	dst_free = dst_area_size * word_size - dst_occupied;
	bytes_to_move = min(dst_free, src_len * entry_size);
	dst_entries = dst_area + dst_occupied;
	src_entries = src_area + count_size;
	memcpy(dst_entries, src_entries, bytes_to_move);
	entries_moved = bytes_to_move / entry_size;

	switch (mode) {
	case KCOV_MODE_TRACE_PC:
	case KCOV_MODE_TRACE_AFL:
		WRITE_ONCE(*(unsigned long *)dst_area, dst_len + entries_moved);
		break;
	case KCOV_MODE_TRACE_CMP:
		WRITE_ONCE(*(u64 *)dst_area, dst_len + entries_moved);
		break;
	default:
		break;
	}
}

void kcov_remote_stop(void)
{
	struct task_struct *t = current;
	struct kcov *kcov = t->kcov;
	void *area = t->kcov_area;
	unsigned int size = t->kcov_size;
	int sequence = t->kcov_sequence;

	if (!kcov)
		return;

	kcov_stop(t);
	t->kcov = NULL;
	t->prev_ip = 0;

	spin_lock(&kcov->lock);
	/*
	 * KCOV_DISABLE could have been called between kcov_remote_start()
	 * and kcov_remote_stop(), hence the check.
	 */
	if (sequence == kcov->sequence && kcov->remote)
		kcov_move_area(kcov->mode, kcov->area, kcov->size, area);
	spin_unlock(&kcov->lock);

	spin_lock(&kcov_remote_lock);
	kcov_remote_area_put(area, size);
#ifdef CONFIG_KCOV_VDEV
	if (kcov_disable_callback) {
		kcov_disable_callback(area, true);
	}
#endif
	spin_unlock(&kcov_remote_lock);

	kcov_put(kcov);
}

static int __init kcov_init(void)
{
	/*
	 * The kcov debugfs file won't ever get removed and thus,
	 * there is no need to protect it against removal races. The
	 * use of debugfs_create_file_unsafe() is actually safe here.
	 */
	debugfs_create_file_unsafe("kcov", 0600, NULL, NULL, &kcov_fops);

	return 0;
}

device_initcall(kcov_init);
