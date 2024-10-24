#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/rbtree.h>
#include <kernel/pt.h>
#include <common/spinlock.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

// typedef struct UserContext {
//     // TODO: customize your trap frame
//     u64 spsr, elr;
//     u64 x[18]; // x0-x18
// } UserContext;

typedef struct UserContext {
    // TODO: customize your trap frame
    u64 spsr;
    u64 elr;
    u64 sp;
    // General Purpose registers
    u64 gregs[31];
} UserContext;

typedef struct KernelContext {
    // TODO: customize your context
    u64 lr, x0, x1;
    u64 x[11]; // x19-x29
} KernelContext;

typedef struct PIDNode {
    ListNode node;
    int pid;
} PIDNode;

// embeded data for procs
struct schinfo {
    // TODO: customize your sched info
    ListNode sched_node;    // 调度队列节点
    bool in_run_queue;    // 是否在就绪队列中
    SpinLock lock;
    ListNode kill_node;
};

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
    struct pgdir pgdir;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;
} Proc;

extern Proc root_proc;
extern ListNode free_pid_list; 
extern SpinLock pid_lock;
extern SpinLock proc_lock;

void init_kproc();
void init_proc(Proc *);
Proc *create_proc();
int start_proc(Proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int *exitcode);
int kill(int pid);

void init_pid_pool(int initial_pid_count);
int allocate_pid();
void deallocate_pid(int pid);
