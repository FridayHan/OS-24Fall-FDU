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

    printk("-------------------------------------------------------\n");
    // printk("Initializing scheduler...\n");

    init_spinlock(&sched_lock);
    init_spinlock(&run_queue_lock);
    init_list_node(&run_queue);

    // 创建idle进程
    for (int i = 0; i < NCPU; i++) {
        Proc *p = kalloc(sizeof(Proc));
        p->idle = 1;
        p->state = RUNNING;
        p->pid = -1;
        cpus[i].sched.idle_proc = cpus[i].sched.thisproc = p;
        // printk("cpus[%d].sched.idle_proc.state: %d\n", i, cpus[i].sched.idle_proc->state);
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

    // printk("Initializing schinfo for process with PID %d\n", container_of(p, Proc, schinfo)->pid);
    init_list_node(&p->sched_node);
    p->in_run_queue = false;
    // printk("Initialized sched_node: next = %p, prev = %p\n", p->sched_node.next, p->sched_node.prev);
    // init_spinlock(&p->lock)
    // printk("schinfo initialization completed for process at address: %p\n", p);
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
    // printk("is_zombie acquiring\n");
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

    // printk("activate_proc acquiring\n");

    // 打印run_queue
    acquire_spinlock(&run_queue_lock);
    // printk("RUN_QUEUE_LOCK acquired\n");
    if (cpuid() != 0) {
        for (ListNode *p = run_queue.next; p != &run_queue; p = p->next) {
            auto proc = container_of(p, Proc, schinfo.sched_node);
            printk("PID %d in run_queue\n", proc->pid);
        }
    }
    release_spinlock(&run_queue_lock);
    // printk("RUN_QUEUE_LOCK released\n");


    
    if (cpuid() != 0 || cpuid() == 0)
    {
        printk("pid: %d activated by cpu %lld\n", p->pid, cpuid());
    }
    acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE) {
        printk("Process PID %d is already RUNNING or RUNNABLE\n", p->pid);
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        acquire_spinlock(&run_queue_lock);
        // printk("RUN_QUEUE_LOCK acquired\n");
        _insert_into_list(&run_queue, &p->schinfo.sched_node);
        release_spinlock(&run_queue_lock);
        // printk("RUN_QUEUE_LOCK released\n");
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
    
    thisproc()->state = new_state;
    // if (new_state == SLEEPING || new_state == ZOMBIE || new_state == RUNNABLE) {
    //     cpus[cpuid()].sched.thisproc = NULL;
    // }
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        if (thisproc()->schinfo.in_run_queue) {
            acquire_spinlock(&run_queue_lock);
            // printk("RUN_QUEUE_LOCK acquired\n");
            _detach_from_list(&thisproc()->schinfo.sched_node);
            release_spinlock(&run_queue_lock);
            // printk("RUN_QUEUE_LOCK released\n");
        }
    }
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

    if (panic_flag) {
        return cpus[cpuid()].sched.idle_proc;
    }
    acquire_spinlock(&run_queue_lock);
    // printk("RUN_QUEUE_LOCK acquired\n");
    for (ListNode *p = run_queue.next; p != &run_queue; p = p->next) {
        // if (p == &run_queue) 
        //     continue;
        auto proc = container_of(p, Proc, schinfo.sched_node);
        // _detach_from_list(&proc->schinfo.sched_node); // ?????
        if (proc->state == RUNNABLE) {
            // printk("Next process to run: PID %d\n", proc->pid);
            release_spinlock(&run_queue_lock);
            // printk("RUN_QUEUE_LOCK released\n");
            return proc;
        }
    }
    release_spinlock(&run_queue_lock);
    // printk("RUN_QUEUE_LOCK released\n");
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
    // printk("Scheduling on CPU %lld, current process PID %d, new state: %d\n", cpuid(), this->pid, new_state); 
    // printk("this->state: %d\n", this->state);
    // printk("this->pid: %d\n", this->pid);

    // printk("this->ptnode: %p\n", &this->ptnode);
    // printk("this->ptnode.next: %p\n", this->ptnode.next);
    // printk("this->ptnode.prev: %p\n", this->ptnode.prev);

    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    // printk("next->pid: %d\n", next->pid);
    // printk("next->state: %d\n", next->state);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    // printk("cpuid: %lld\n", cpuid());
    // printk("Current process PID %d, new state: %d\n", this->pid, this->state);
    // printk("Next process to run: PID %d, state: %d\n", next->pid, next->state);
    // printk("p->kcontext->lr: %llx\n", next->kcontext->lr);
    if (next != this) {
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
    // printk("Scheduling completed on CPU %lld\n", cpuid());
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    // printk("Entering process entry point with arg: %llu\n", arg);
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
