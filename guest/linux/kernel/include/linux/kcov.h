/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KCOV_H
#define _LINUX_KCOV_H

#include <uapi/linux/kcov.h>

struct task_struct;

#ifdef CONFIG_KCOV

enum kcov_mode {
	/* Coverage collection is not enabled yet. */
	KCOV_MODE_DISABLED = 0,
	/* KCOV was initialized, but tracing mode hasn't been chosen yet. */
	KCOV_MODE_INIT = 1,
	/*
	 * Tracing coverage collection mode.
	 * Covered PCs are collected in a per-task buffer.
	 */
	KCOV_MODE_TRACE_PC = 2,
	/* Collecting comparison operands mode. */
	KCOV_MODE_TRACE_CMP = 3,
	/*
	 * AFL-style collection.
	 * Covered branches are hashed and collected in a fixed size buffer
	 * (see AFL documentation for more information).
	 */
	KCOV_MODE_TRACE_AFL = 4,
};

#define KCOV_IN_CTXSW	(1 << 30)


#define kcov_prepare_switch(t)			\
do {						\
	(t)->kcov_mode |= KCOV_IN_CTXSW;	\
} while (0)

#define kcov_finish_switch(t)			\
do {						\
	(t)->kcov_mode &= ~KCOV_IN_CTXSW;	\
} while (0)

/* See Documentation/dev-tools/kcov.rst for usage details. */
void kcov_remote_start(u64 handle);
void kcov_remote_stop(void);
u64 kcov_common_handle(void);

static inline void kcov_remote_start_common(u64 id)
{
	kcov_remote_start(kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, id));
}

static inline void kcov_remote_start_usb(u64 id)
{
	kcov_remote_start(kcov_remote_handle(KCOV_SUBSYSTEM_USB, id));
}

#ifdef CONFIG_KCOV_VDEV
int kcov_open_g(void);
int kcov_mmap_g(void);
int kcov_close_g(void);
void kcov_register_enable_callback(void (*cb)(void *, unsigned int, bool));
void kcov_register_disable_callback(void (*cb)(void *, bool));
void kcov_register_flush_callback(void (*cb)(void *, unsigned int));
uint64_t kcov_get_area_offset_g(void);
int kcov_ioctl_locked_g(unsigned int cmd, unsigned long arg);
#endif
void kcov_task_init(struct task_struct *t);
void kcov_task_exit(struct task_struct *t);

#else

static inline void kcov_task_init(struct task_struct *t) {}
static inline void kcov_task_exit(struct task_struct *t) {}
static inline void kcov_prepare_switch(struct task_struct *t) {}
static inline void kcov_finish_switch(struct task_struct *t) {}
static inline void kcov_remote_start(u64 handle) {}
static inline void kcov_remote_stop(void) {}
static inline u64 kcov_common_handle(void)
{
	return 0;
}
static inline void kcov_remote_start_common(u64 id) {}
static inline void kcov_remote_start_usb(u64 id) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
