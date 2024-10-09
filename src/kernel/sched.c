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
ListNode run_queue;

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU

    printk("-------------------------------------------------------\n");
    printk("Initializing scheduler...\n");

    init_spinlock(&sched_lock);

    // 创建idle进程
    for (int i = 0; i < NCPU; i++) {
        Proc *p = kalloc(sizeof(Proc));
        p->idle = 1;
        p->state = RUNNING;
        cpus[i].sched.idle_proc = cpus[i].sched.thisproc = p;
    }
}

Proc *thisproc()
{
    // TODO: return the current process
    Proc *p = cpus[cpuid()].sched.thisproc;
    // printk("Current process on CPU %lld: PID %d, state %d\n", cpuid(), p->pid, p->state);
    return p;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process

    printk("Initializing schinfo for process with PID %d\n", container_of(p, Proc, schinfo)->pid);
    init_list_node(&p->sched_node);
    // printk("Initialized sched_node: next = %p, prev = %p\n", p->sched_node.next, p->sched_node.prev);
    // init_spinlock(&p->lock)
    // printk("schinfo initialization completed for process at address: %p\n", p);
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need

    printk("Acquiring.\n");
    acquire_spinlock(&sched_lock);
    printk("Acquired.\n");
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need

    printk("Releasing.\n");
    release_spinlock(&sched_lock);
    printk("Released.\n");
}

bool is_zombie(Proc *p)
{
    bool r;
    printk("is_zombie acquiring\n");
    acquire_sched_lock();
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

    // printk("Activating process PID %d, current state: %d\n", p->pid, p->state);

    printk("activate_proc acquiring\n");
    acquire_sched_lock();
    cpus[cpuid()].sched.thisproc = p;
    if (p->state == RUNNING || p->state == RUNNABLE) {
        printk("Process PID %d is already RUNNING or RUNNABLE\n", p->pid);
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        printk("Process PID %d state changed to RUNNABLE\n", p->pid);
        _insert_into_list(&run_queue, &p->schinfo.sched_node);
        printk("Process PID %d added to run queue\n", p->pid);
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
    
    thisproc()->state = new_state;
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        _detach_from_list(&thisproc()->schinfo.sched_node);
    }
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

    // printk("Picking next process to run...\n");
    _for_in_list(p, &run_queue) {
        if (p == &run_queue) 
            continue;
        auto proc = container_of(p, Proc, schinfo.sched_node);
        // _detach_from_list(&proc->schinfo.sched_node); // ?????
        if (proc->state == RUNNABLE) {
            // printk("Next process to run: PID %d\n", proc->pid);
            return proc;
        }
    }
    return cpus[cpuid()].sched.idle_proc;
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process

    // printk("Updating current process on CPU %lld to PID %d\n", cpuid(), p->pid);
    cpus[cpuid()].sched.thisproc = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    // if (this->state == RUNNING) {
    //     printk("STATE:RUNNING\n");
    // } else if (this->state == RUNNABLE) {
    //     printk("STATE:RUNNABLE\n");
    // } else if (this->state == SLEEPING) {
    //     printk("STATE:SLEEPING\n");
    // } else if (this->state == ZOMBIE) {
    //     printk("STATE:ZOMBIE\n");
    // } else if (this->state == UNUSED) {
    //     printk("STATE:UNUSED\n");
    // }
    // printk("Scheduling on CPU %lld, current process PID %d, new state: %d\n", cpuid(), this->pid, new_state); 
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
    printk("Entering process entry point with arg: %llu\n", arg);
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
