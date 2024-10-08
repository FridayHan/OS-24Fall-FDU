#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/cpu.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <driver/gicv3.h>
#include <driver/timer.h>

static volatile bool boot_secondary_cpus = false;

void main()
{
    if (cpuid() == 0) {
        extern char edata[], end[];
        memset(edata, 0, (usize)(end - edata));

        /* initialize interrupt handler */
        init_interrupt();

        uart_init();
        printk_init();

        // printk("Hello, world! (Core 0)\n");

        gicv3_init();
        gicv3_init_percpu();

        timer_init(1000);
        timer_init_percpu();

        /* initialize kernel memory allocator */
        kinit();

        /* initialize kernel proc */
        init_kproc();

        /* initialize sched */
        init_sched();

        smp_init();

        arch_fence();

        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
    } else {
        while (!boot_secondary_cpus)
            ;
        arch_fence();
        printk("Hello, world! (Core %lld)\n", cpuid());
        timer_init_percpu();
        gicv3_init_percpu();
    }

    set_return_addr(idle_entry);
}
