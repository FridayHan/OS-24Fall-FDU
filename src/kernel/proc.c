#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <fs/defines.h>

Proc root_proc;
SpinLock proc_lock;

void kernel_entry();
void proc_entry();

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)

    printk("Initializing kernel process...\n");

    // init_spinlock(&proc_lock);
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    printk("Root process initialized. PID: %d\n", root_proc.pid);

    start_proc(&root_proc, kernel_entry, 123456);
    printk("Kernel process started. Entry: %p\n", kernel_entry);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    printk("Initializing process...\n");

    static int nextpid = 1;

    acquire_sched_lock();
    memset(p, 0, sizeof(Proc));
    p->pid = nextpid++;
    printk("Assigned PID: %d\n", p->pid);
    release_sched_lock();

    p->kstack = kalloc(KSTACK_SIZE);
    if (!p->kstack) {
        PANIC();
    }
    printk("Kernel stack allocated: %p\n", p->kstack);

    p->ucontext = kalloc(sizeof(UserContext));
    if (!p->ucontext) {
        PANIC();
    }
    printk("User context allocated: %p\n", p->ucontext);

    p->kcontext = kalloc(sizeof(KernelContext));
    if (!p->kcontext) {
        PANIC();
    }
    printk("Kernel context allocated: %p\n", p->kcontext);

    p->state = UNUSED;
    p->parent = NULL;
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_sched();
    init_schinfo(&p->schinfo);

    printk("Process initialized with PID: %d\n", p->pid);
}

Proc *create_proc()
{
    printk("Creating new process...\n");
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    printk("Process created with PID: %d\n", p->pid);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL

    acquire_sched_lock();
    proc->parent = thisproc();
    _insert_into_list(&proc->parent->children, &proc->ptnode);
    release_sched_lock();
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    
    printk("Starting process with PID: %d\n", p->pid);

    if (p->parent == NULL) {
        set_parent_to_this(p);
        printk("Parent set to current process. Parent PID: %d\n", p->parent->pid);
    }

    memset(p->kcontext, 0, sizeof(KernelContext));

    // Set sp to the top of the kernel stack
    p->kcontext->sp = (u64)p->kstack + KSTACK_SIZE;

    // Set lr to proc_entry
    p->kcontext->lr = (u64)proc_entry;

    // Set x19 and x20 to entry and arg
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;

    printk("Kernel context set for process with PID: %d, entry: %p, arg: %llu\n", p->pid, entry, arg);

    activate_proc(p);
    printk("Process activated. PID: %d\n", p->pid);

    return p->pid;
}

int wait(int *exitcode)
{
    // TODO:
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency

    Proc *p = thisproc();
    printk("Waiting for child process. Parent PID: %d\n", p->pid);

    acquire_sched_lock();
    if (_empty_list(&p->children)) {
        release_sched_lock();
        printk("No children for process with PID: %d\n", p->pid);
        return -1;
    }
    while (1) {
        // Check for any zombie child
        // ListNode *node;
        _for_in_list(node, &p->children) {
            Proc *cp = container_of(node, Proc, ptnode);
            if (cp->state == ZOMBIE) {
                // Found a zombie child
                int pid = cp->pid;
                if (exitcode) {
                    *exitcode = cp->exitcode;
                }
                // Remove from children list
                _detach_from_list(&cp->ptnode);
                // Free child's resources
                kfree(cp->kstack);
                kfree(cp->ucontext);
                kfree(cp->kcontext);
                kfree(cp);
                // release();
                printk("Child process with PID: %d exited. Exit code: %d\n", pid, *exitcode);
                return pid;
            }
        }
        // No zombie child found, sleep
        // release();
        printk("No zombie child found. Sleeping...\n");
        wait_sem(&p->childexit);
        // acquire();
    }
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency

    Proc *p = thisproc();
    printk("Process with PID: %d exiting. Exit code: %d\n", p->pid, code);

    // acquire_spinlock();
    p->exitcode = code;

    // Re-parent children to root_proc
    // ListNode *node;
    _for_in_list(node, &p->children) {
        Proc *cp = container_of(node, Proc, ptnode);
        
        // 将子进程从当前进程的子进程列表中移除
        _detach_from_list(&cp->ptnode);
        
        // 将子进程的父进程设置为 root_proc
        cp->parent = &root_proc;
        _insert_into_list(&root_proc.children, &cp->ptnode);

        // 如果子进程是 ZOMBIE，唤醒 root_proc
        if (cp->state == ZOMBIE) {
            post_sem(&root_proc.childexit);
        }
    }
    // Wake up parent
    post_sem(&p->parent->childexit);

    p->state = ZOMBIE;
    printk("Process with PID: %d set to ZOMBIE state.\n", p->pid);

    // release();

    sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}
