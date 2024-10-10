#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/rbtree.h>
#include <common/spinlock.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

// typedef struct UserContext {
//     // TODO: customize your trap frame
//     u64 regs[31];
//     u64 sp;
//     u64 pc;
//     u64 pstate;
// } UserContext;

// typedef struct KernelContext {
//     // TODO: customize your context
//     u64 sp;
//     u64 x19;
//     u64 x20;
//     u64 x21;
//     u64 x22;
//     u64 x23;
//     u64 x24;
//     u64 x25;
//     u64 x26;
//     u64 x27;
//     u64 x28;
//     u64 fp;
//     u64 lr;
// } KernelContext;

typedef struct UserContext {
    // TODO: customize your trap frame
    u64 spsr, elr;
    u64 x[18]; // x0-x18
} UserContext;

typedef struct KernelContext {
    // TODO: customize your context
    u64 lr, x0, x1;
    u64 x[11]; // x19-x29
} KernelContext;

// embeded data for procs
struct schinfo {
    // TODO: customize your sched info
    ListNode sched_node;    // 调度队列节点
    // int priority;           // 进程优先级
    // int cpu_affinity;       // CPU亲和性（-1表示任何CPU）
    // u64 time_slice;         // 剩余时间片
    bool in_run_queue;    // 是否在就绪队列中
};

// extern SpinLock proc_lock;

typedef struct Proc {
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct Proc *parent;
    struct schinfo schinfo;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;
} Proc;

void init_kproc();
void init_proc(Proc *);
Proc *create_proc();
int start_proc(Proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int *exitcode);
