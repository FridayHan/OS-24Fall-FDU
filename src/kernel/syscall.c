#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void init_syscall()
{
    for (u64 *p = (u64 *)&early_init; p < (u64 *)&rest_init; p++)
        ((void (*)()) * p)();
}

void *syscall_table[NR_SYSCALL] = {
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    // be sure to check the range of id. if id >= NR_SYSCALL, panic.

    // printk("syscall_entry\n");
    u64 syscall_id = context->x[8];
    u64 x[5];
    for (int i = 0; i <= 5; i++) {
        x[i] = context->x[i];
    }
    if (syscall_id >= NR_SYSCALL || syscall_table[syscall_id] == NULL) {
        printk("Invalid syscall ID: %llu\n", syscall_id);
        PANIC();
    }
    // if (syscall_id == SYS_myreport) {
    //     printk("%lld: syscall_myreport(%llu)\n", cpuid(), x[0]);
    // }

    context->x[0] = ((u64 (*)(u64, u64, u64, u64, u64, u64))syscall_table[syscall_id])(x[0], x[1], x[2], x[3], x[4], x[5]);
    // u64 (*syscall_func)(u64, u64, u64, u64, u64, u64) = syscall_table[syscall_id];
    // context->x[0] = syscall_func(arg0, arg1, arg2, arg3, arg4, arg5);
}

/** 
 * Check if the virtual address [start,start+size) is READABLE by the current
 * user process.
 */
bool user_readable(const void *start, usize size) {
    /* (Final) TODO BEGIN */
    return true;
    /* (Final) TODO END */
}


/**
 * Check if the virtual address [start,start+size) is READABLE & WRITEABLE by
 * the current user process.
 */
bool user_writeable(const void *start, usize size) {
    /* (Final) TODO Begin */
    return true;
    /* (Final) TODO End */
}

/** 
 * Get the length of a string including tailing '\0' in the memory space of
 * current user process return 0 if the length exceeds maxlen or the string is
 * not readable by the current user process.
 */
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}