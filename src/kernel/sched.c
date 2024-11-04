#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
#include <common/spinlock.h>
#include <driver/clock.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

SpinLock sched_lock;
ListNode run_queue;

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU

    init_spinlock(&sched_lock);
    init_list_node(&run_queue);

    for (int i = 0; i < NCPU; i++) {
        Proc *p = kalloc(sizeof(Proc));
        if (!p) {
            PANIC();
        }
        memset(p, 0, sizeof(Proc));
        p->idle = true;
        p->state = RUNNING;
        p->parent = NULL;
        p->pid = -1;
        p->killed = false;
        p->parent = NULL;
        p->schinfo.in_run_queue = false;
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

    acquire_spinlock(&sched_lock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need

    release_spinlock(&sched_lock);
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

bool activate_proc(Proc *p)
{
    // TODO:
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic

    acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        _insert_into_list(&run_queue, &p->schinfo.sched_node);
        p->schinfo.in_run_queue = true;
    } else {
        PANIC();
        release_sched_lock();
        return false;
    }
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and not [remove it from the sched queue if
    // new_state=SLEEPING/ZOMBIE]

    if ((new_state == SLEEPING || new_state == ZOMBIE) && (thisproc()->state == RUNNABLE)) {
        if (thisproc()->schinfo.in_run_queue) { // TODO:rebundant
            _detach_from_list(&thisproc()->schinfo.sched_node);
            thisproc()->schinfo.in_run_queue = false;
        }
    }
    else if (new_state == RUNNABLE && !thisproc()->idle) {
        if (!thisproc()->schinfo.in_run_queue) {
            _insert_into_list(run_queue.prev, &thisproc()->schinfo.sched_node);
            thisproc()->schinfo.in_run_queue = true;
        }
    }
    thisproc()->state = new_state;
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

    if (panic_flag)
        return cpus[cpuid()].sched.idle_proc;

    auto p = run_queue.next;
    if (p == &run_queue) {
        return cpus[cpuid()].sched.idle_proc;
    }

    auto proc = container_of(p, Proc, schinfo.sched_node);
    _detach_from_list(&proc->schinfo.sched_node);
    proc->schinfo.in_run_queue = false;
    ASSERT(proc->state == RUNNABLE);
    return proc;
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process

    // thisproc()->start_time = get_timestamp_ms();
    cpus[cpuid()].sched.thisproc = p;
}

void sched(enum procstate new_state)
{
    auto this = thisproc();
    if (this->killed && new_state != ZOMBIE) {
        release_sched_lock();
        return;
    }
    
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        attach_pgdir(&next->pgdir);
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
