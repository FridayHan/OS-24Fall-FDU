#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/virtio.h>
#include <common/buf.h>

volatile bool panic_flag;

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
    init_filesystem();

    printk("Hello world! (Core %lld)\n", cpuid());
    proc_test();
    vm_test();
    user_proc_test();
    io_test();
    // pgfault_first_test();
    // pgfault_second_test();

    /* LAB 4 TODO 3 BEGIN */
    Buf b;
    b.flags = 0;
    b.block_no = (u32)0x0;
    virtio_blk_rw(&b);
    u8 *data = b.data;
    int LBA = *(int *)(data + 0x1CE + 0x8);
    int num = *(int *)(data + 0x1CE + 0xC);
    printk("LBA:%d, num:%d\n", LBA, num);
    /* LAB 4 TODO 3 END */

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */
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