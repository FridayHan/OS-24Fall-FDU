#pragma once

#include <kernel/proc.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>

void init_sched();
void init_schinfo(struct schinfo *);

bool activate_proc(Proc *);
bool is_zombie(Proc *);
bool is_unused(Proc *);
void acquire_sched_lock();
void release_sched_lock();
void sched(enum procstate new_state);

void init_sched_timer(int i);
void sched_timer_callback(struct timer *t);

// MUST call lock_for_sched() before sched() !!!
#define yield() (acquire_sched_lock(), sched(RUNNABLE))

Proc *thisproc();
