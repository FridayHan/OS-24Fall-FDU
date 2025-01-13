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
static struct rb_root_ run_tree;
static struct timer sched_timer[NCPU];

static bool __timer_cmp(rb_node lnode, rb_node rnode)
{
    i64 d = container_of(lnode, struct schinfo, rb_sched_node)->vruntime -
            container_of(rnode, struct schinfo, rb_sched_node)->vruntime;
    if (d < 0)
        return true;
    if (d == 0)
        return lnode < rnode;
    return false;
}

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU

    init_spinlock(&sched_lock);

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

    p->vruntime = 0;
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

bool _activate_proc(Proc *p, bool onalert)
{
    // TODO:(Lab5 new)
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEPING, do nothing if onalert or activate it if else, and return the corresponding value.

    acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        if (_rb_insert(&p->schinfo.rb_sched_node, &run_tree, __timer_cmp))
            PANIC();
    } else if (p->state == DEEPSLEEPING) {
        if (onalert) {
            release_sched_lock();
            return false;
        }
        else {
            p->state = RUNNABLE;
            if (_rb_insert(&p->schinfo.rb_sched_node, &run_tree, __timer_cmp))
                PANIC();
        }
    }
    else {
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
        _rb_erase(&thisproc()->schinfo.rb_sched_node, &run_tree);
    }
    else if (new_state == RUNNABLE && !thisproc()->idle) {
        if (_rb_insert(&thisproc()->schinfo.rb_sched_node, &run_tree, __timer_cmp))
            PANIC();
    }
    thisproc()->state = new_state;
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

    if (panic_flag)
        return cpus[cpuid()].sched.idle_proc;

    auto next = _rb_first(&run_tree);

    if (next)
    {
        auto proc = container_of(next, Proc, schinfo.rb_sched_node);
        return proc;
    }
    return cpus[cpuid()].sched.idle_proc;
}

static void sched_timer_callback(struct timer *t) {
    t->data--;
    thisproc()->schinfo.vruntime += TIMESLICE;
    acquire_sched_lock();
    sched(RUNNABLE);
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process

    if (sched_timer[cpuid()].data > 0) {
        cancel_cpu_timer(&sched_timer[cpuid()]);
        sched_timer[cpuid()].data--;
    }
    cpus[cpuid()].sched.thisproc = p;

    sched_timer[cpuid()].elapse = TIMESLICE;
    sched_timer[cpuid()].handler = sched_timer_callback;

    set_cpu_timer(&sched_timer[cpuid()]);
    sched_timer[cpuid()].data++;

    ASSERT(p->state == RUNNABLE);
    if (!p->idle)
    {
        _rb_erase(&p->schinfo.rb_sched_node, &run_tree);
    }
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
