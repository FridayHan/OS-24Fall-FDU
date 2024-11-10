#pragma once

#include <kernel/proc.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>

// 进程调度：CFS(完全公平调度)算法
// https://blog.csdn.net/weixin_39713838/article/details/88055881
static const int prio_to_weight[40]={
/* -20 */ 88761, 71755, 56483, 46273, 36291,
/* -15 */ 29154, 23254, 18705, 14949, 11916,
/* -10 */ 9548, 7620, 6100, 4904, 3906,
/* -5 */ 3121, 2501, 1991, 1586, 1277,
/* 0 */ 1024, 820, 655, 526, 423,
/* 5 */ 335, 272, 215, 172, 137,
/* 10 */ 110, 87, 70, 56, 45,
/* 15 */ 36, 29, 23, 18, 15
};

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
