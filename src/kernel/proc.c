#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <fs/defines.h>

Proc root_proc;
ListNode free_pid_list; 
static int next_pid = INITIAL_PID_COUNT;
SpinLock pid_lock;
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
    
    init_pid_pool(INITIAL_PID_COUNT);
    init_proc(&root_proc);
    init_spinlock(&proc_lock);
    root_proc.state = UNUSED;
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency

    acquire_spinlock(&proc_lock);

    p->killed = false;
    p->idle = false;
    p->pid = allocate_pid();
    p->state = UNUSED;
    p->parent = NULL;
    p->exitcode = 0;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);
    init_pgdir(&p->pgdir);

    p->kstack = kalloc(KSTACK_SIZE);
    if (!p->kstack) {
        PANIC();
    }
    memset(p->kstack, 0, KSTACK_SIZE);

    p->kcontext = (KernelContext *)(p->kstack + KSTACK_SIZE - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext *)(p->kstack + KSTACK_SIZE - sizeof(UserContext));
    ASSERT(sizeof(KernelContext) + sizeof(UserContext) <= KSTACK_SIZE);

    release_spinlock(&proc_lock);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    memset(p, 0, sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL

    acquire_spinlock(&proc_lock);
    proc->parent = thisproc();
    ASSERT(proc->pid != 0);
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    release_spinlock(&proc_lock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    
    if (p->parent == NULL) {
        acquire_spinlock(&proc_lock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        release_spinlock(&proc_lock);
    }

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

    Proc *this = thisproc();
    if (_empty_list(&this->children)) {
        return -1;
    }

    wait_sem(&this->childexit);
    acquire_spinlock(&proc_lock);

    _for_in_list(node, &this->children)
    {
        if (node == &this->children)
            continue;
        Proc *cp = container_of(node, Proc, ptnode);
        if (is_zombie(cp)) {
            int pid = cp->pid;
            _detach_from_list(node);

            if (exitcode) {
                *exitcode = cp->exitcode;
            }
            kfree(cp->kstack);
            kfree(cp);
            release_spinlock(&proc_lock);
            return pid;
        }
    }

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

    Proc *this = thisproc();
    acquire_spinlock(&proc_lock);
    this->exitcode = code;

    while(!_empty_list(&this->children)) {
        ListNode *node = this->children.next;
        Proc *cp = container_of(node, Proc, ptnode);
        _detach_from_list(node);
        cp->parent = &root_proc;
        ASSERT(cp->pid != 0);
        _insert_into_list(root_proc.children.prev, node);
        if (is_zombie(cp)) {
            post_sem(&root_proc.childexit);
        }
    }

    post_sem(&this->parent->childexit);

    // free_pgdir(&this->pgdir);

    deallocate_pid(this->pid);

    // post_sem(&this->parent->childexit);
    acquire_sched_lock();
    release_spinlock(&proc_lock);

    sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

Proc *dfs(Proc *p, int pid)
{
    if (p->pid == pid) {
        return p;
    } else if (!_empty_list(&p->children)) {
        _for_in_list(node, &p->children) {
            if (node == &p->children) {
                continue;
            }
            Proc *cp = container_of(node, Proc, ptnode);
            Proc *ret = dfs(cp, pid);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
// dfs kill
    acquire_spinlock(&proc_lock);
    Proc *p = dfs(&root_proc, pid);
    if (p && !is_unused(p)) {
        p->killed = true;
        activate_proc(p);
        release_spinlock(&proc_lock);
        return 0;
    }
    release_spinlock(&proc_lock);
    return -1;

// bfs kill
    // acquire_spinlock(&proc_lock);

    // ListNode queue;
    // init_list_node(&queue);
    // _insert_into_list(&queue, &root_proc.schinfo.kill_node);

    // while (!_empty_list(&queue)) {
    //     ListNode *node = queue.next;
    //     Proc *p = container_of(node, Proc, schinfo.kill_node);
    //     if (p->pid == pid && p->state != UNUSED) {
    //         p->killed = true;
    //         release_spinlock(&proc_lock);
    //         activate_proc(p);
    //         return 0;
    //     }

    //     _for_in_list(node2, &p->children) {
    //         Proc *p0 = container_of(node2, Proc, ptnode);
    //         if (p0->pid == p->pid || p0->pid < 0) {
    //             continue;
    //         }
    //         ListNode *kill_node = &p0->schinfo.kill_node;
    //         _insert_into_list(&queue, kill_node);
    //     }
    //     _detach_from_list(node);
    // }
    // release_spinlock(&proc_lock);
    // return -1;
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork()
{
    /**
     * (Final) TODO BEGIN
     * 
     * 1. Create a new child process.
     * 2. Copy the parent's memory space.
     * 3. Copy the parent's trapframe.
     * 4. Set the parent of the new proc to current proc.
     * 5. Set the state of the new proc to RUNNABLE.
     * 6. Activate the new proc and return its pid.
     */
    // 1. 创建新的子进程
    Proc *child = create_proc();
    if (!child) {
        return -1;
    }

    // 2. 复制父进程的内存空间
    copy_sections(&thisproc()->pgdir.section_head, &child->pgdir.section_head);

    // 3. 复制父进程的 trapframe
    *child->ucontext = *thisproc()->ucontext;

    // 4. 设置父进程为当前进程
    set_parent_to_this(child);

    // 5. 设置子进程状态为 RUNNABLE
    child->state = RUNNABLE;

    // 6. 激活子进程并返回其 pid
    activate_proc(child);
    return child->pid;
}

void init_pid_pool(int initial_pid_count) {
    init_list_node(&free_pid_list);
    init_spinlock(&pid_lock);
    for (int i = initial_pid_count - 1; i >= 0; i--) {
        PIDNode *pid_node = kalloc(sizeof(PIDNode));
        memset(pid_node, 0, sizeof(PIDNode));
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
    memset(pid_node, 0, sizeof(PIDNode));
    pid_node->pid = pid;
    _insert_into_list(&free_pid_list, &pid_node->node);
    release_spinlock(&pid_lock);
}