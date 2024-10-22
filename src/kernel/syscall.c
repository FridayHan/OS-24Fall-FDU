#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

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

    // 从 UserContext 提取系统调用号 (x8)
    u64 syscall_id = context->x[8];

    // 检查系统调用号是否超出范围
    if (syscall_id >= NR_SYSCALL || syscall_table[syscall_id] == NULL) {
        printk("Invalid syscall ID: %llu\n", syscall_id);
        PANIC();
    }

    // 提取系统调用参数，存储在 x0-x5
    u64 arg0 = context->x[0];
    u64 arg1 = context->x[1];
    u64 arg2 = context->x[2];
    u64 arg3 = context->x[3];
    u64 arg4 = context->x[4];
    u64 arg5 = context->x[5];

    // 根据系统调用号查找对应的系统调用函数
    u64 (*syscall_func)(u64, u64, u64, u64, u64, u64) = syscall_table[syscall_id];

    // 调用系统调用函数，并将结果存储在 x0 中作为返回值
    context->x[0] = syscall_func(arg0, arg1, arg2, arg3, arg4, arg5);
}

#pragma GCC diagnostic pop