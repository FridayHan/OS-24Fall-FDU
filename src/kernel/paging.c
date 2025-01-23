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
    // 初始化section链表
    init_list_node(section_head);

    // struct section *text_section = kalloc(sizeof(struct section));
    // text_section->flags = ST_TEXT;
    // text_section->begin = 0x10000000;  // 假设代码段的起始地址
    // text_section->end = 0x20000000;    // 假设代码段的结束地址
    // _insert_into_list(section_head, &text_section->stnode);

    // struct section *data_section = kalloc(sizeof(struct section));
    // data_section->flags = ST_DATA;
    // data_section->begin = 0x20000000;
    // data_section->end = 0x30000000;
    // _insert_into_list(section_head, &data_section->stnode);

    // // TODO: other sections
    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    _for_in_list(node, &pd->section_head) {
        if (node == &pd->section_head) {
            continue; // 跳过链表头节点
        }

        struct section *sec = container_of(node, struct section, stnode);

        // 如果是文件映射区域，需要处理文件相关的资源
        if (sec->flags & ST_FILE) {
            // 释放文件相关资源
            if (sec->fp) {
                // TODO: 释放或关闭文件描述符
                // 可以考虑将文件的引用计数减1，当计数为0时释放文件

            }
        }

        // TODO: 如果是其他类型的映射（例如堆/栈），释放相关资源
        // 这里可以根据具体情况处理物理页面的释放等

        // 移除该section节点并释放section结构体
        _detach_from_list(node);
        kfree(sec);  // 释放section结构体
    }
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
    // 遍历进程的section链表，找到包含该地址的section
    struct section *fault_section = NULL;
    _for_in_list(node, &pd->section_head) {
        if (node == &pd->section_head) continue;

        struct section *sec = container_of(node, struct section, stnode);
        printk("section begin: %llx, end: %llx\n", sec->begin, sec->end);
        if (addr >= sec->begin && addr < sec->end) {
            fault_section = sec;
            break;
        }
    }

    if (!fault_section) {
        // 如果找不到对应的section，处理为非法访问
        printk("Page fault at addr %llx: invalid address\n", addr);
        return -1;
    }

    // 根据section的flags来判断处理逻辑
    if (fault_section->flags & ST_RO) {
        // 只读区域，发生写操作导致页面故障
        printk("Write to read-only memory at addr %llx\n", addr);
        return -1;  // 错误处理
    } else if (fault_section->flags & ST_SWAP) {
        // 如果是swap区，可能需要将页交换回内存
        printk("Handle swap page fault for addr %llx\n", addr);
        // TODO: 从swap中加载数据
    } else if (fault_section->flags & ST_FILE) {
        // 文件映射区域，可能需要从文件加载数据
        printk("File-backed memory access at addr %llx\n", addr);
        // TODO: 从文件中加载数据
    } else {
        // 未知的处理方式
        printk("Unknown page fault at addr %llx\n", addr);
        return -1;  // 错误处理
    }
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
