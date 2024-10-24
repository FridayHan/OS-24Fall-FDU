#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
#include <common/spinlock.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

SpinLock sched_lock;
SpinLock run_queue_lock;
ListNode run_queue;

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU

    init_spinlock(&sched_lock);
    init_spinlock(&run_queue_lock);
    init_list_node(&run_queue);

    for (int i = 0; i < NCPU; i++) {
        Proc *p = kalloc(sizeof(Proc));
        if (!p) {
            PANIC();
        }
        p->idle = 1;
        p->state = RUNNING;
        p->parent = NULL;
        p->pid = -1;
        cpus[i].sched.idle_proc = cpus[i].sched.thisproc = p;
    }
}

Proc *thisproc()
{
    // TODO: return the current process

    Proc *p = cpus[cpuid()].sched.thisproc;
    return p;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process

    init_list_node(&p->sched_node);
    p->in_run_queue = false;
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need

    // printk("Acquiring.\n");
    acquire_spinlock(&sched_lock);
    // printk("Acquired.\n");
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need

    // printk("Releasing.\n");
    release_spinlock(&sched_lock);
    // printk("Released.\n");
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    // printk("is_zombie acquire_sched_lock\n");
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool activate_proc(Proc *p)
{
    // TODO:
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic

    // acquire_spinlock(&run_queue_lock);
    // if (cpuid() != 0) {
    //     for (ListNode *p = run_queue.next; p != &run_queue; p = p->next) {
    //         auto proc = container_of(p, Proc, schinfo.sched_node);
    //         printk("PID %d in run_queue\n", proc->pid);
    //     }
    // }
    // release_spinlock(&run_queue_lock);

    // if (1)
    // {
    //     printk("activate_proc executing on CPU %lld\n", cpuid());
    // }

    acquire_sched_lock();
    // printk("activate_proc acquire_sched_lock\n");
    printk("%lld: activate_proc: PID %d\n", cpuid(), p->pid);
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        acquire_spinlock(&run_queue_lock);
        _insert_into_list(&run_queue, &p->schinfo.sched_node);
        release_spinlock(&run_queue_lock);
        p->schinfo.in_run_queue = true;
    } else {
        printk("PANIC: Unexpected process state for PID %d\n", p->pid);
        PANIC();
    }
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and not [remove it from the sched queue if
    // new_state=SLEEPING/ZOMBIE]
    
    // if (cpuid() != 0 && thisproc()->pid != -1)
    // {
    //     printk("update_this_state executing on CPU %lld\n", cpuid());
    // }

    printk("%lld: update_this_state: PID %d to %d\n",cpuid(), thisproc()->pid, new_state);

    thisproc()->state = new_state;
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        if (thisproc()->schinfo.in_run_queue) {
            acquire_spinlock(&run_queue_lock);
            _detach_from_list(&thisproc()->schinfo.sched_node);
            thisproc()->schinfo.in_run_queue = false;
            release_spinlock(&run_queue_lock);
        }
    }
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    // if (thisproc()->pid > 4 && cpuid() == 0)
    // {
    //     return cpus[cpuid()].sched.idle_proc;
    // }

    if (panic_flag) {
        return cpus[cpuid()].sched.idle_proc;
    }
    acquire_spinlock(&run_queue_lock);
    _for_in_list(p, &run_queue) {
        auto proc = container_of(p, Proc, schinfo.sched_node);
        if (proc->state == RUNNABLE) {
            release_spinlock(&run_queue_lock);
            printk("%lld: next pid: %d\n", cpuid(), proc->pid);
            return proc;
        }
    }
    // for (ListNode *p = run_queue.next; p != &run_queue; p = p->next) {
    //     auto proc = container_of(p, Proc, schinfo.sched_node);
    //     if (proc->state == RUNNABLE) {
    //         release_spinlock(&run_queue_lock);
    //         return proc;
    //     }
    // }
    release_spinlock(&run_queue_lock);

    printk("%lld: next pid: %d\n", cpuid(), -1);
    return cpus[cpuid()].sched.idle_proc;
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process

    printk("%lld: running PID %d\n", cpuid(), p->pid);
    cpus[cpuid()].sched.thisproc = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    printk("%lld: sched: PID %d to %d\n", cpuid(), this->pid, new_state);
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
