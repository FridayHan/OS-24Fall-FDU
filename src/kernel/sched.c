#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
#include <common/spinlock.h>

#define sched_latency 20
#define min_lantency 1

extern bool panic_flag;
static struct timer sched_timer[NCPU];
static u64 uptime[NCPU];
int weight_sum = 0;

static struct rb_root_ root = {NULL};

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

SpinLock sched_lock;
rb_node run_queue;

inline static u64 min_vruntime()
{
    auto t = _rb_first(&root);
    if (t == NULL)
        return thisproc()->schinfo.vruntime;
    return container_of(t, struct schinfo, sched_node)->vruntime;
}

// 比较两个进程，返回true表示lnode的优先级更高
bool cmp(rb_node lnode, rb_node rnode)
{
    auto l = container_of(lnode, struct Proc, schinfo.sched_node);
    auto r = container_of(rnode, struct Proc, schinfo.sched_node);
    if (l->schinfo.vruntime == r->schinfo.vruntime)
        return l->pid < r->pid;
    else
        return l->schinfo.vruntime < r->schinfo.vruntime;
}

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU

    init_spinlock(&sched_lock);
    // init_spinlock(&run_queue_lock);
    root.rb_node = NULL;

    for (int i = 0; i < NCPU; i++) {
        Proc *p = kalloc(sizeof(Proc));
        if (!p) {
            PANIC();
        }
        memset(p, 0, sizeof(Proc));
        p->idle = 1;
        p->state = RUNNING;
        p->parent = NULL;
        p->pid = -1;
        p->killed = false;
        p->parent = NULL;
        p->schinfo.in_run_queue = false;
        cpus[i].sched.idle_proc = cpus[i].sched.thisproc = p;
        // init_sched_timer(i);
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

    // init_list_node(&p->sched_node);
    init_list_node(&p->kill_node);
    p->in_run_queue = false;
    p->vruntime = 0;
    p->prio = 21;
    p->weight = prio_to_weight[p->prio];
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need

    // printk("Acquiring.\n");
    acquire_spinlock(&sched_lock);
    // printk("%lld: Acquired.\n", cpuid());
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need

    // printk("Releasing.\n");
    release_spinlock(&sched_lock);
    // printk("%lld: Released.\n", cpuid());
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    // // printk("is_zombie acquire_sched_lock\n");
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

    // printk("%lld: activate_proc acquiring\n", cpuid());
    acquire_sched_lock();
    // printk("%lld: activate_proc: PID %d\n", cpuid(), p->pid);
    if (p->state == RUNNING || p->state == RUNNABLE) {
        // printk("%lld: activate_proc releasing\n", cpuid());
        release_sched_lock();
        return false;
    } else if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        // acquire_spinlock(&run_queue_lock);

        // _insert_into_list(&run_queue, &p->schinfo.sched_node);
        // p->schinfo.in_run_queue = true;

        p->schinfo.vruntime = MAX(p->schinfo.vruntime, min_vruntime());
        weight_sum += p->schinfo.weight;
        _rb_insert(&p->schinfo.sched_node, &root, cmp);

        // _for_in_list(node, &run_queue) {
        //     auto proc = container_of(node, Proc, schinfo.sched_node);
        //     printk("PID: %d\n", proc->pid);
        // }
        // release_spinlock(&run_queue_lock);

    } else {
        release_sched_lock();
        return false;
    }
    // printk("%lld: activate_proc releasing\n", cpuid());
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and not [remove it from the sched queue if
    // new_state=SLEEPING/ZOMBIE]

    // printk("%lld: update_this_state: PID %d to %d\n", cpuid(), thisproc()->pid, new_state);
    auto p = thisproc();
    p->state = new_state;
    if ((new_state == SLEEPING || new_state == ZOMBIE) && (thisproc()->state == RUNNABLE)) {
        // if (thisproc()->schinfo.in_run_queue) { // TODO:rebundant
        //     // acquire_spinlock(&run_queue_lock);
        //     // _detach_from_list(&thisproc()->schinfo.sched_node);
        //     // thisproc()->schinfo.in_run_queue = false;
        //     // release_spinlock(&run_queue_lock);
        // }
        weight_sum -= p->schinfo.weight;
    }
    else if (new_state == RUNNABLE && !thisproc()->idle) {
        if (!thisproc()->schinfo.in_run_queue) {
            // acquire_spinlock(&run_queue_lock);

            // _insert_into_list(run_queue.prev, &thisproc()->schinfo.sched_node);
            p->schinfo.vruntime = MAX(p->schinfo.vruntime, min_vruntime());
            _rb_insert(&p->schinfo.sched_node, &root, cmp);

            // thisproc()->schinfo.in_run_queue = true;
            // release_spinlock(&run_queue_lock);
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
    // acquire_spinlock(&run_queue_lock);

    // auto p = run_queue.next;
    auto node = _rb_first(&root);
    // if (p == &run_queue) {
    //     // release_spinlock(&run_queue_lock);
    //     return cpus[cpuid()].sched.idle_proc;
    // }
    if (node == NULL) {
        // release_spinlock(&run_queue_lock);
        return cpus[cpuid()].sched.idle_proc;
    }
    _rb_erase(node, &root);
    return container_of(node, Proc, schinfo.sched_node);
    // auto proc = container_of(p, Proc, schinfo.sched_node);
    // release_spinlock(&run_queue_lock);
    // printk("proc->state: %d, proc->pid: %d\n", proc->state, proc->pid);
    // _detach_from_list(&proc->schinfo.sched_node);
    // proc->schinfo.in_run_queue = false;
    // printk("PICK: pid: %d, cpuid: %lld, state: %d\n", proc->pid, cpuid(), proc->state);
    // ASSERT(proc->state == RUNNABLE);
    // return proc;
    // if (proc->state == RUNNABLE) {
    //     release_spinlock(&run_queue_lock);
    //     // printk("PICK: pid: %d, cpuid: %lld\n", proc->pid, cpuid());
    //     return proc;
    // }
    // printk("PICK: pid: -1, cpuid: %lld\n", cpuid());
    // release_spinlock(&run_queue_lock);
    // return cpus[cpuid()].sched.idle_proc;
}

// static void update_this_proc(Proc *p)
// {
//     // TODO: you should implement this routinue
//     // update thisproc to the choosen process

//     // printk("update_this_proc: PID %d, cpuid %lld\n", p->pid, cpuid());
//     // acquire_spinlock(&run_queue_lock);

//     // if (thisproc()->schinfo.in_run_queue) {
//     //     _detach_from_list(&thisproc()->schinfo.sched_node);
//     //     thisproc()->schinfo.in_run_queue = false;
//     // }
    
//     // release_spinlock(&run_queue_lock);
//     cpus[cpuid()].sched.thisproc = p;
    

//     // if (!p->idle) {
//     //     init_sched_timer(cpuid());
//     // }

//     // if (!p->idle)
//     // {
//     // if (sched_timer[cpuid()].data > 0) {
//     //     cancel_cpu_timer(&sched_timer[cpuid()]);
//     //     sched_timer[cpuid()].data--;
//     // }

//     // cpus[cpuid()].sched.thisproc = p;

//     // sched_timer[cpuid()].elapse = 2;
//     // sched_timer[cpuid()].handler = sched_timer_callback;

//     // set_cpu_timer(&sched_timer[cpuid()]);
//     // sched_timer[cpuid()].data++;
//     // }


//     // ASSERT(p->state == RUNNABLE);


//     // cancel_cpu_timer(&sched_timer[cpuid()]);
//     // sched_timer[cpuid()].elapse = 2;
//     // sched_timer[cpuid()].handler = sched_timer_callback;
//     // set_cpu_timer(&sched_timer[cpuid()]);
// }

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    while (sched_timer[cpuid()].data) {
        sched_timer[cpuid()].data--;
        cancel_cpu_timer(&sched_timer[cpuid()]);
    }
    sched_timer[cpuid()].data++;
    sched_timer[cpuid()].elapse =
            MAX(sched_latency * p->schinfo.weight / MAX(weight_sum, 15),
                min_lantency);
    sched_timer[cpuid()].handler = sched_timer_callback;
    uptime[cpuid()] = get_timestamp();
    set_cpu_timer(&sched_timer[cpuid()]);

    cpus[cpuid()].sched.thisproc = p;
}

void sched(enum procstate new_state)
{
    auto this = thisproc();
    // printk("%lld: sched: PID %d\n", cpuid(), this->pid);
    if (this->killed && new_state != ZOMBIE) {
        // printk("%lld: sched releasing\n", cpuid());
        release_sched_lock();
        return;
    }
    
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    // printk("sched: PID %d, cpuid: %lld\n", this->pid, cpuid());
    auto next = pick_next();
    // printk("sched: PID %d, cpuid: %lld\n", next->pid, cpuid());
    update_this_proc(next);
    // printk("sched: PID %d, cpuid: %lld\n", this->pid, cpuid());
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
        // init_sched_timer(cpuid());
    }
    // printk("%lld: sched releasing\n", cpuid());
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    // printk("%lld: proc_entry releasing\n", cpuid());
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}

// void init_sched_timer(int i) {
//     sched_timer[i].elapse = 10;
//     sched_timer[i].handler = sched_timer_callback;
//     set_cpu_timer(&sched_timer[i]);
// }



void sched_timer_callback(struct timer *t) {
    // release_sched_lock();
    sched_timer[cpuid()].data--;
    acquire_sched_lock();
    sched(RUNNABLE);
}