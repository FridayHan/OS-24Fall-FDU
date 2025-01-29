#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>
#include <../musl/obj/include/bits/syscall.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

extern u64 sys_execve();

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

    u64 syscall_id = context->x[8];
    u64 x[5];
    for (int i = 0; i <= 5; i++)
    {
        x[i] = context->x[i];
    }

    if (syscall_id >= NR_SYSCALL || syscall_table[syscall_id] == NULL)
    {
        printk("Invalid syscall ID: %llu\n", syscall_id);
        PANIC();
    }

    context->x[0] = ((u64 (*)(u64, u64, u64, u64, u64, u64))syscall_table[syscall_id])(x[0], x[1], x[2], x[3], x[4], x[5]);
}

bool user_accessible(const void *start, usize size, bool check_writeable)
{
    bool ret = false;
    ListNode head = thisproc()->pgdir.section_head;
    _for_in_list(node, &head)
    {
        if (node == &head) continue;
        Section* st = container_of(node, Section, stnode);
        
        if (st->begin <= (u64)start && ((u64)start + size) <= st->end)
        {
            if (check_writeable)
            {
                if (st->flags != ST_TEXT)
                {
                    ret = true;
                }
            }
            else
            {
                ret = true;
            }
            break;
        }
    }
    return ret;
}

/** 
 * Check if the virtual address [start,start+size) is READABLE by the current
 * user process.
 */
bool user_readable(const void *start, usize size)
{
    /* (Final) TODO BEGIN */
    return user_accessible(start, size, false);
    /* (Final) TODO END */
}


/**
 * Check if the virtual address [start,start+size) is READABLE & WRITEABLE by
 * the current user process.
 */
bool user_writeable(const void *start, usize size)
{
    /* (Final) TODO Begin */
    return user_accessible(start, size, true);
    /* (Final) TODO End */
}

/** 
 * Get the length of a string including tailing '\0' in the memory space of
 * current user process return 0 if the length exceeds maxlen or the string is
 * not readable by the current user process.
 */
usize user_strlen(const char *str, usize maxlen)
{
    for (usize i = 0; i < maxlen; i++)
    {
        if (user_readable(&str[i], 1))
        {
            if (str[i] == 0)
                return i + 1;
        }
        else
            return 0;
    }
    return 0;
}