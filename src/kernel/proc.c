#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <fs/defines.h>

Proc root_proc;

void kernel_entry();
void proc_entry();

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)

    // init_spinlock(&proc_lock);

    // init the root_proc

    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency

    static int nextpid = 1;

    // acquire_sched_lock(&proc_lock);
    memset(p, 0, sizeof(Proc));
    p->pid = nextpid++;
    // release(&proc_lock);

    p->kstack = kalloc(KSTACK_SIZE);
    if (!p->kstack) {
        PANIC();
    }

    p->ucontext = kalloc(sizeof(UserContext));
    if (!p->ucontext) {
        PANIC();
    }

    p->kcontext = kalloc(sizeof(KernelContext));
    if (!p->kcontext) {
        PANIC();
    }

    p->state = UNUSED;
    p->parent = NULL;
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);
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

    // acquire(&proc_lock);
    proc->parent = thisproc();
    _insert_into_list(&proc->parent->children, &proc->ptnode);
    // release(&proc_lock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    
    if (p->parent == NULL) {
        set_parent_to_this(p);
    }

    memset(p->kcontext, 0, sizeof(KernelContext));

    // Set sp to the top of the kernel stack
    p->kcontext->sp = (u64)p->kstack + KSTACK_SIZE;

    // Set lr to proc_entry
    p->kcontext->lr = (u64)proc_entry;

    // Set x19 and x20 to entry and arg
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;

    // Activate the process
    activate_proc(p);

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
    // acquire(&proc_lock);
    if (_empty_list(&p->children)) {
        // release(&proc_lock);
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
                // release(&proc_lock);
                return pid;
            }
        }
        // No zombie child found, sleep
        // release(&proc_lock);
        wait_sem(&p->childexit);
        // acquire(&proc_lock);
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
    // acquire_spinlock(&proc_lock);
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

    // release(&proc_lock);

    sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}
