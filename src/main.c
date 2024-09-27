#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

static volatile bool boot_secondary_cpus = false;

void main() {
    if (cpuid() == 0) {
        extern char edata[], end[];
        memset(edata, 0, (usize)(end - edata));

        smp_init();
        uart_init();
        printk_init();

        printk("Hello, world! (Core 0)\n");

        /* initialize kernel memory allocator */
        kinit();

        arch_fence();

        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
    } else {
        while (!boot_secondary_cpus);
        arch_fence();
        printk("Hello, world! (Core %lld)\n", cpuid());
    }

    set_return_addr(idle_entry);
}
