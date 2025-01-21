#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>


void init_sections(ListNode *section_head) {
    /* (Final) TODO BEGIN */

    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}

u64 sbrk(i64 size) {
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    return 0;    
    /* (Final) TODO END */
}

int pgfault_handler(u64 iss) {
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 addr =
            arch_get_far(); // Attempting to access this address caused the page fault

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */
    printk("pd: %p\n", pd);
    printk("addr: %lld\n", addr);
    return 0;
    /* (Final) TODO END */
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */

    // 遍历父进程的section列表
    _for_in_list(node, from_head) {
        if (node == from_head) {
            continue;
        }
        struct section *from_section = container_of(node, struct section, stnode);

        // 分配新的section结构体
        struct section *to_section = kalloc(sizeof(struct section));
        if (!to_section) {
            PANIC();
        }
        memcpy(to_section, from_section, sizeof(struct section));

        // 复制文件指针和相关数据
        if (from_section->flags & ST_FILE) {
            to_section->fp = from_section->fp;
            to_section->offset = from_section->offset;
            to_section->length = from_section->length;
        }

        // 插入到子进程的section列表
        _insert_into_list(to_head, &to_section->stnode);
    }

    /* (Final) TODO END */
}
