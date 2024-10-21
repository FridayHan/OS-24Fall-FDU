#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <fs/defines.h>

Proc root_proc;
ListNode free_pid_list; 
static int next_pid = INITIAL_PID_COUNT;
SpinLock pid_lock;
SpinLock proc_lock;
static int pid;
// SpinLock proc_lock;

void kernel_entry();
void proc_entry();

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)
    
    init_pid_pool(INITIAL_PID_COUNT);
    init_proc(&root_proc);
    // init_spinlock(&proc_lock);
    init_sem(&root_proc.childexit, 0);
    root_proc.state = UNUSED;
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency

    memset(p, 0, sizeof(Proc));

    p->killed = false;
    p->idle = false;
    p->pid = allocate_pid();
    p->state = UNUSED;
    p->parent = NULL;
    p->exitcode = 0;
    p->schinfo.in_run_queue = false;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);
    init_spinlock(&p->schinfo.lock);

    p->kstack = kalloc(KSTACK_SIZE);
    if (!p->kstack) {
        PANIC();
    }
    p->kcontext = (KernelContext *)(p->kstack + KSTACK_SIZE - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext *)(p->kstack + KSTACK_SIZE - sizeof(UserContext));
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL

    acquire_spinlock(&proc->schinfo.lock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    release_spinlock(&proc->schinfo.lock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    
    if (p->parent == NULL) {
        acquire_spinlock(&p->schinfo.lock);
        acquire_spinlock(&root_proc.schinfo.lock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        // printk("Parent: %d, Child: %d\n", 0, p->pid);
        release_spinlock(&root_proc.schinfo.lock);
        release_spinlock(&p->schinfo.lock);
    }

    // printk("start_proc: PID: %d\n", p->pid);

    p->kcontext->lr = (u64)proc_entry;  
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = arg;

    int id = p->pid;
    activate_proc(p);
    return id;
}

int wait(int *exitcode)
{
    // TODO:
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency

    // if (1)
    // {
    //     printk("wait PID: %d executing on CPU %lld\n", thisproc()->pid, cpuid());
    // }

    Proc *p = thisproc();

    for (ListNode *node = p->children.next; node != &p->children; node = node->next)
    {
        // printk("Parent: %d, Child: %d\n", p->pid, container_of(node, Proc, ptnode)->pid);
    }
    
    if (_empty_list(&p->children)) {
        return -1;
    }

    // wait_sem(&p->childexit);

    while (1)
    {
        wait_sem(&p->childexit);
    wait_sem(&p->childexit);
    acquire_sched_lock();
    _for_in_list(node, &p->children)
    {
        Proc *cp = container_of(node, Proc, ptnode);
        if (is_zombie(cp)) {
            int pid = cp->pid;
            acquire_spinlock(&p->schinfo.lock);
            _detach_from_list(node);
            release_spinlock(&p->schinfo.lock);
            // acquire_spinlock(&proc_lock);
            if (exitcode) {
                *exitcode = cp->exitcode;
            }
            kfree(cp->kstack);
            kfree(cp);
            // release_spinlock(&proc_lock);
            release_sched_lock();
            return pid;
        }
    }
    }

    printk("wait PID: %d no zombie\n", p->pid);
    PANIC();
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency

    // if (1)
    // {
    //     printk("exit PID: %d executing on CPU %lld\n", thisproc()->pid, cpuid());
    // }

    Proc *p = thisproc();
    acquire_spinlock(&proc_lock);
    acquire_spinlock(&p->schinfo.lock);
    acquire_sched_lock();
    p->exitcode = code;

    printk("exit: PID: %d, cpuid: %lld\n", p->pid, cpuid());
    while(!_empty_list(&p->children)) {
        ListNode *node = p->children.next;
        Proc *cp = container_of(node, Proc, ptnode);
        acquire_spinlock(&cp->schinfo.lock);
        _detach_from_list(node);
        cp->parent = &root_proc;
        release_spinlock(&cp->schinfo.lock);
        acquire_spinlock(&root_proc.schinfo.lock);
        _insert_into_list(&root_proc.children, node);
        release_spinlock(&root_proc.schinfo.lock);
        if (is_zombie(cp)) {
            post_sem(&root_proc.childexit);
        }
    }

    post_sem(&p->parent->childexit);

    release_spinlock(&p->schinfo.lock);
    release_spinlock(&proc_lock);
    deallocate_pid(p->pid);
    acquire_sched_lock();
    printk("exit acquire_sched_lock\n");
    acquire_sched_lock();
    release_sched_lock();
    sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).

    acquire_sched_lock();
    _for_in_list(node, &root_proc.children)
    {
        // if (node == &root_proc.children)
        //     continue;
        Proc *p = container_of(node, Proc, ptnode);
        if (p->pid == pid) {
            p->killed = true;
            release_sched_lock();
            return 0;
        }
    }
    release_sched_lock();
    return -1;
}

void init_pid_pool(int initial_pid_count) {
    init_list_node(&free_pid_list);
    init_spinlock(&pid_lock);
    for (int i = initial_pid_count - 1; i >= 0; i--) {
        PIDNode *pid_node = kalloc(sizeof(PIDNode));
        pid_node->pid = i;
        _insert_into_list(&free_pid_list, &pid_node->node);
    }
}

int allocate_pid() {
    acquire_spinlock(&pid_lock);
    if (!_empty_list(&free_pid_list)) {
        ListNode *node = free_pid_list.next;
        _detach_from_list(free_pid_list.next);
        PIDNode *pid_node = container_of(node, PIDNode, node);
        int pid = pid_node->pid;
        kfree(pid_node);
        release_spinlock(&pid_lock);
        return pid;
    }
    int pid = next_pid++;
    release_spinlock(&pid_lock);
    return pid;
}

void deallocate_pid(int pid) {
    acquire_spinlock(&pid_lock);
    PIDNode *pid_node = kalloc(sizeof(PIDNode));
    pid_node->pid = pid;
    _insert_into_list(&free_pid_list, &pid_node->node);
    release_spinlock(&pid_lock);
}