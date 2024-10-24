#include <aarch64/trap.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>

void trap_global_handler(UserContext *context)
{
    thisproc()->ucontext = context;

    u64 esr = arch_get_esr();
    u64 ec = esr >> ESR_EC_SHIFT;
    u64 iss = esr & ESR_ISS_MASK;
    u64 ir = esr & ESR_IR_MASK;

    (void)iss;

    arch_reset_esr();

    switch (ec) {
    case ESR_EC_UNKNOWN: {
        if (ir)
            PANIC();
        else
            interrupt_global_handler();
    } break;
    case ESR_EC_SVC64: {
        syscall_entry(context);
    } break;
    case ESR_EC_IABORT_EL0:
    case ESR_EC_IABORT_EL1:
    case ESR_EC_DABORT_EL0:
    case ESR_EC_DABORT_EL1: {
        // printk("Page fault\n");
        u64 fsc = iss & 0x3F;  // Fault Status Code (FSC) 位[5:0]

        printk("Page fault occurred on CPU: %lld! FSC code: %llu\n", cpuid(), fsc);
        printk("Faulting address: 0x%llx\n", arch_get_far());
        printk("ELR: 0x%llx\n", context->elr);
        printk("SPSR: 0x%llx\n", context->spsr);
        printk("SP: 0x%llx\n", context->sp);
        printk("EC: 0x%llx\n", ec);
        printk("IL: %llu\n", (iss >> 25) & 0x1);
        printk("ISS: 0x%llx\n", iss);
        printk("IFSC: %llu\n", (iss >> 10) & 0x3F);
        printk("DFSC: %llu\n", (iss >> 0) & 0x3F);
        printk("SAS: %llu\n", (iss >> 22) & 0x3);
        printk("SET: %llu\n", (iss >> 21) & 0x1);
        printk("FnV: %llu\n", (iss >> 25) & 0x1);
        printk("EA: %llu\n", (iss >> 24) & 0x1);
        printk("CM: %llu\n", (iss >> 23) & 0x1);
        printk("WnR: %llu\n", (iss >> 6) & 0x1);
        printk("CM: %llu\n", (iss >> 4) & 0x3);

        // switch (fsc) {
        // case 0b000001: printk("Address size fault at level 0\n"); break;
        // case 0b000011: printk("Address size fault at level 1\n"); break;
        // case 0b000101: printk("Address size fault at level 2\n"); break;
        // case 0b000111: printk("Address size fault at level 3\n"); break;
        // case 0b010000: printk("Access flag fault at level 1\n"); break;
        // case 0b010001: printk("Access flag fault at level 2\n"); break;
        // case 0b010010: printk("Access flag fault at level 3\n"); break;
        // case 0b100001: printk("Translation fault at level 0\n"); break;
        // case 0b100011: printk("Translation fault at level 1\n"); break;
        // case 0b100101: printk("Translation fault at level 2\n"); break;
        // case 0b100111: printk("Translation fault at level 3\n"); break;
        // default: printk("Unknown FSC code: %llu\n", fsc); break;
        // }
        PANIC();
    } break;
    default: {
        printk("Unknwon exception %llu\n", ec);
        PANIC();
    }
    }

    // TODO: stop killed process while returning to user space
    if (thisproc()->killed) {
        exit(-1);
    }
}

NO_RETURN void trap_error_handler(u64 type)
{
    printk("Unknown trap type %llu\n", type);
    PANIC();
}
