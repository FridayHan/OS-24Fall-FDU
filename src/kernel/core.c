#include <aarch64/intrinsic.h>
#include <common/buf.h>
#include <common/string.h>
#include <driver/virtio.h>
#include <kernel/cpu.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>

#define INIT_ELR 0x400000
#define INIT_SP 0x80000000
#define INIT_SPSR 0x0
#define INIT_SIZE ((u64)eicode - (u64)icode)

u32 LBA;
volatile bool panic_flag;
// extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
void trap_return();
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
    printk("Hello world! (Core %lld)\n", cpuid());
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

    Proc *init_proc = create_proc();
    init_proc->ucontext->x[0] = 0;
    init_proc->ucontext->elr = INIT_ELR;
    init_proc->ucontext->spsr = INIT_SPSR;
    init_proc->ucontext->sp = INIT_SP;

    Section *sec = (Section *)kalloc(sizeof(Section));
    init_section(sec);
    if (!sec)
    {
        PANIC();
    }
    sec->flags = ST_TEXT;
    sec->begin = INIT_ELR;
    sec->end = INIT_ELR + INIT_SIZE;
    
    // printk("init_proc->pgdir: %p\n", &init_proc->pgdir);
    // printk("init_proc->pgdir.section_head: %p\n", &init_proc->pgdir.section_head);
    // printk("sec: %p\n", sec);
    // printk("sec->stnode: %p\n", &sec->stnode);
    _insert_into_list(&init_proc->pgdir.section_head, &sec->stnode);

    void *p = kalloc_page();
    if (!p)
    {
        PANIC();
    }
    memset(p, 0, PAGE_SIZE);
    memcpy(p, (void*)icode, PAGE_SIZE);
    vmmap(&init_proc->pgdir, INIT_ELR, p, PTE_USER_DATA | PTE_RO);
    start_proc(init_proc, trap_return, 0);
    printk("Create process %d\n", init_proc->pid);

    while (1)
    {
        int a;
        a = wait(&a);
    }

    // *get_pte(&init_proc->pgdir, 0, true) = (K2P(page) | PTE_USER_DATA | PTE_RO);
    // attach_pgdir(&init_proc->pgdir);
    // arch_tlbi_vmalle1is();
    // init_proc->kcontext = (KernelContext *)((u64)init_proc->ucontext - sizeof(KernelContext));
    // printk("init_proc->kcontext: %p\n", init_proc->kcontext);
    // printk("init_proc->ucontext: %p\n", init_proc->ucontext);
    // swtch(init_proc->kcontext, &thisproc()->kcontext);

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