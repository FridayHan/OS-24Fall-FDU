#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/virtio.h>
#include <common/buf.h>
#include <kernel/mem.h>

u32 LBA;
volatile bool panic_flag;
extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
extern void trap_return();
extern char icode[], eicode[];

NO_RETURN void idle_entry()
{
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
        {
            printk("CPU %lld: PANIC! Stopped.\n", cpuid());
            break;
        }
        arch_with_trap
        {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry()
{
    // printk("Hello world! (Core %lld)\n", cpuid());
    // proc_test();
    // vm_test();
    // user_proc_test();
    // io_test();
    // pgfault_first_test();
    // pgfault_second_test();

    /* LAB 4 TODO 3 BEGIN */
    Buf b;
    b.flags = 0;
    b.block_no = (u32)0x0;
    virtio_blk_rw(&b);
    u8 *data = b.data;
    LBA = *(int *)(data + 0x1CE + 0x8);
    int num = *(int *)(data + 0x1CE + 0xC);
    printk("LBA:%d, num:%d\n", LBA, num);
    /* LAB 4 TODO 3 END */
    init_filesystem();

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */
    // 为新的进程创建用户空间上下文
    UserContext *uc = (UserContext *)kalloc(sizeof(UserContext));
    
    // 初始化用户上下文
    u64 init_addr = (u64)(&icode);  // 获取 init.S 中的 icode 地址
    u64 init_sp = (u64)uc + sizeof(UserContext);  // 用户栈指针初始化为上下文的末尾

    uc->spsr = 0x3c5;  // SPSR (Saved Program Status Register) 设置为用户态的EL1
    uc->elr = init_addr;  // 设置ELR (Exception Link Register) 为icode地址
    uc->sp = init_sp;  // 设置栈指针为用户栈
    for (int i = 0; i < 31; i++) {
        uc->x[i] = 0;  // 清空 x0-x30 寄存器
    }

    // 创建进程并设置上下文
    Proc *p = create_proc();
    Page *page = kalloc_page();
    memset(page, 0, PAGE_SIZE);
    memcpy(page, (void*)icode, (usize)(eicode - icode));
    *get_pte(&p->pgdir, 0, true) = (K2P(page) | PTE_USER_DATA | PTE_RO);
    // 配置页表并切换到用户空间的页表
    attach_pgdir(&p->pgdir);
    arch_tlbi_vmalle1is();
    p->kcontext->lr = (u64)trap_return;  // 返回到 trap_return
    // p->kcontext->x0 = (u64)uc;  // 将用户上下文的地址传递给 trap_return
    // p->kcontext->x1 = 0;  // 可以传递一些其他参数
    p->ucontext->elr = 0;
    p->ucontext->spsr = 0;
    // printk("p->kcontext: %p\n", p->kcontext);
    // printk("p->kcontext: %p\n", p->kcontext);
    // printk("p->ucontext: %p\n", p->ucontext);
    // p->kcontext = (KernelContext *)((u64)p->ucontext - sizeof(KernelContext));
    printk("p->kcontext: %p\n", p->kcontext);
    printk("p->ucontext: %p\n", p->ucontext);
    printk("Create process %d\n", p->pid);

    // 切换到新创建的进程
    swtch(p->kcontext, &thisproc()->kcontext);

    PANIC();
    
    /* (Final) TODO END */
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}