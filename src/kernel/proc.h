#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/rbtree.h>
#include <kernel/pt.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <common/spinlock.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext {
    // TODO: customize your trap frame
    u64 res, tpidr_el0, q0_high, q0_low, spsr, elr, sp;
    u64 x[31];
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
    struct rb_node_ rb_sched_node;
    u64 vruntime;
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
    struct oftable oftable;
    Inode *cwd;
} Proc;

extern Proc root_proc;
extern ListNode free_pid_list; 
extern SpinLock pid_lock;
extern SpinLock proc_lock;

void init_kproc();
void init_proc(Proc *);
WARN_RESULT Proc *create_proc();
int start_proc(Proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode);
WARN_RESULT int kill(int pid);
WARN_RESULT int fork();

void init_pid_pool(int initial_pid_count);
int allocate_pid();
void deallocate_pid(int pid);
