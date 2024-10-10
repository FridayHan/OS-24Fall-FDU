#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <fs/defines.h>

Proc root_proc;
// SpinLock proc_lock;
static int pid;

void kernel_entry();
void proc_entry();

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)

    // printk("Initializing kernel process...\n");

    // init_spinlock(&proc_lock);
    init_proc(&root_proc);
    root_proc.state = UNUSED;
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
    acquire_sched_lock();
    sched(RUNNABLE);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    // printk("Initializing process...\n");

    // printk("init_proc acquiring\n");
    memset(p, 0, sizeof(Proc));
    // printk("Assigned PID: %d\n", p->pid);

    p->killed = false;
    p->idle = false;
    acquire_sched_lock();
    p->pid = pid++;
    release_sched_lock();
    p->state = UNUSED;
    p->parent = NULL;
    p->exitcode = 0; // ???
    p->schinfo.in_run_queue = false;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);

    p->kstack = kalloc(KSTACK_SIZE);
    if (!p->kstack) {
        PANIC();
    }

    p->kcontext = (KernelContext *)(p->kstack + KSTACK_SIZE - sizeof(KernelContext) - sizeof(UserContext));

    p->ucontext = (UserContext *)(p->kstack + KSTACK_SIZE - sizeof(UserContext));

    // printk("Process initialized with PID: %d\n", p->pid);
}

Proc *create_proc()
{
    // printk("Creating new process...\n");
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL

    // printk("set_parent_to_this acquiring\n");
    acquire_sched_lock();
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    release_sched_lock();
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    
    // printk("Starting process with PID: %d\n", p->pid);
    if (p->parent == NULL) {
        acquire_sched_lock();
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        printk("Parent: %d, Child: %d\n", 0, p->pid);
        release_sched_lock();
    }

    p->kcontext->lr = (u64)proc_entry;  
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = arg;

    // printk("proc_entry: %p\n", proc_entry);
    // printk("entry: %p\n", entry);
    // printk("arg: %llu\n", arg);

    // printk("p->kcontext->lr: %llx\n", p->kcontext->lr);
    // printk("p->kcontext->x0: %llx\n", p->kcontext->x0);
    // printk("p->kcontext->x1: %llx\n", p->kcontext->x1);



    // // Set sp to the top of the kernel stack
    // p->kcontext->sp = (u64)p->kstack + KSTACK_SIZE;

    // // Set x19 and x20 to entry and arg
    // p->kcontext->x19 = (u64)entry;
    // p->kcontext->x20 = arg;


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

    Proc *p = thisproc();

    printk("wait acquiring\n");
    acquire_sched_lock();
    if (_empty_list(&p->children)) {
        release_sched_lock();
        printk("BOMB\n");
        return -1;
    }

    while (1) {
        printk("current process: %d\n", p->pid);
        _for_in_list(node, &p->children) {
            if (node == &p->children) {
                continue;
            }
            Proc *cp = container_of(node, Proc, ptnode);
            printk("child process: %d\n", cp->pid);
            if (cp->state == ZOMBIE) {
                int pid = cp->pid;
                if (exitcode) {
                    *exitcode = cp->exitcode;
                }
                _detach_from_list(&cp->ptnode);
                kfree(cp->kstack);
                kfree(cp->ucontext);
                kfree(cp->kcontext);
                kfree(cp);
                release_sched_lock();
                return pid;
            }
        }
        release_sched_lock();
        wait_sem(&p->childexit);
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
    p->exitcode = code;
    printk("exit acquiring\n");
    acquire_sched_lock();

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

    release_sched_lock();

    printk("exit thisproc->pid: %d\n", thisproc()->pid);
    post_sem(&p->parent->childexit);
    // p->state = ZOMBIE;
    acquire_sched_lock();
    sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}
