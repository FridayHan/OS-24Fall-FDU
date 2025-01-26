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

#define FAULT_STATUS_CODE_MASK 0x3f

#define ADDRESS_SIZE_FAULT_0 0b000000
#define ADDRESS_SIZE_FAULT_1 0b000001
#define ADDRESS_SIZE_FAULT_2 0b000010
#define ADDRESS_SIZE_FAULT_3 0b000011

#define TRANSLATION_FAULT_0 0b000100
#define TRANSLATION_FAULT_1 0b000101
#define TRANSLATION_FAULT_2 0b000110
#define TRANSLATION_FAULT_3 0b000111

#define ACCESS_FLAG_FAULT_0 0b001000
#define ACCESS_FLAG_FAULT_1 0b001001
#define ACCESS_FLAG_FAULT_2 0b001010
#define ACCESS_FLAG_FAULT_3 0b001011

#define PERMISSION_FAULT_0 0b001100
#define PERMISSION_FAULT_1 0b001101
#define PERMISSION_FAULT_2 0b001110
#define PERMISSION_FAULT_3 0b001111

void init_section(struct section *sec)
{
    memset(sec, 0, sizeof(struct section));
    init_list_node(&sec->stnode);
}

void init_sections(ListNode *section_head)
{
    /* (Final) TODO BEGIN */
    init_list_node(section_head);
    /* (Final) TODO END */
}

void free_section_pages(struct pgdir *pd, struct section *sec)
{
    u64 begin = PAGE_BASE(sec->begin);
    u64 end = sec->end;
    
    for (u64 addr = begin; addr < end; addr += PAGE_SIZE)
    {
        PTEntriesPtr pte = get_pte(pd, addr, false);
        if (pte && (*pte & PTE_VALID))
        {
            kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
            *pte = NULL;
        }
    }
}

void free_section(struct pgdir *pd, struct section *sec)
{
    free_section_pages(pd, sec);
    if (sec->fp) file_close(sec->fp);
    kfree(sec);
}

void free_sections(struct pgdir *pd)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pd->lock);
    struct section *prev_sec = NULL;
    _for_in_list(node, &pd->section_head)
    {
        if (node == &pd->section_head) continue;
        struct section *sec = container_of(node, struct section, stnode);
        free_section(pd, sec);
        if (!prev_sec) _detach_from_list(&prev_sec->stnode);
        prev_sec = sec;
    }
    release_spinlock(&pd->lock);
    /* (Final) TODO END */
}

u64 sbrk(i64 size)
{
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    ASSERT(size % PAGE_SIZE == 0);
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    struct section *heap_sec;

    acquire_spinlock(&pd->lock);
    ASSERT((heap_sec = lookup_section(pd, ST_HEAP)));

    if (size == 0)
    {
        release_spinlock(&pd->lock);
        return heap_sec->end;
    }

    u64 prev_heap_end = heap_sec->end;
    heap_sec->end += size;

    if (size > 0) ASSERT(heap_sec->end > prev_heap_end);
    else
    {
        ASSERT(heap_sec->end < prev_heap_end);

        for (u64 i = heap_sec->end; i < prev_heap_end; i += PAGE_SIZE)
        {
            PTEntriesPtr pte = get_pte(pd, i, false);
            if (pte && (*pte & PTE_VALID))
            {
                kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
                *pte = 0;
            }
        }
    }

    release_spinlock(&pd->lock);
    return prev_heap_end;
    /* (Final) TODO END */
}

struct section *lookup_section(struct pgdir *pd, u64 va)
{
    printk("lookup_section: pd=%p, va=%p\n", pd, (void *)va);
    _for_in_list(node, &pd->section_head)
    {
        if (node == &pd->section_head) continue;
        struct section *sec = container_of(node, struct section, stnode);
        if (va >= sec->begin && va < sec->end)
            return sec;
    }
    return NULL;
}

void handle_missing_pte(struct pgdir *pd, u64 addr, struct section *fault_sec)
{
    printk("PTE missing: pd=%p, addr=%p, fault_sec=%p\n", pd, (void *)addr, fault_sec);
    PTEntriesPtr pte = get_pte(pd, addr, true);
    void* pg = kalloc_page();

    if ((fault_sec->flags & ST_HEAP) == ST_HEAP)
    {
        *pte = (K2P(pg) | PTE_USER_DATA | PTE_RW);
    }
    else if ((fault_sec->flags & ST_TEXT) == ST_TEXT)
    {
        usize i = PAGE_BASE(addr) < fault_sec->begin ? fault_sec->begin : PAGE_BASE(addr);
        usize start = fault_sec->offset + (i - fault_sec->begin);
        usize n = fault_sec->end - i > PAGE_SIZE ? PAGE_SIZE : fault_sec->end - i;
        usize little_off = VA_OFFSET(i);

        inodes.lock(fault_sec->fp->ip);
        inodes.read(fault_sec->fp->ip, (u8 *)pg + little_off, start, n);
        inodes.unlock(fault_sec->fp->ip);
        *pte = (K2P(pg) | PTE_USER_DATA | PTE_RO);
    }
    else if ((fault_sec->flags & ST_FILE) == ST_FILE)
    {
        usize i = PAGE_BASE(addr) < fault_sec->begin ? fault_sec->begin : PAGE_BASE(addr);
        usize start = fault_sec->offset + (i - fault_sec->begin);
        usize n = fault_sec->end - i > PAGE_SIZE ? PAGE_SIZE : fault_sec->end - i;
        usize little_off = VA_OFFSET(i);

        inodes.lock(fault_sec->fp->ip);
        inodes.read(fault_sec->fp->ip, (u8 *)pg + little_off, start, n);
        inodes.unlock(fault_sec->fp->ip);
        *pte = (K2P(pg) | PTE_USER_DATA | PTE_RW);
    }
    else PANIC();
}

int handle_permission_fault(struct pgdir *pd, u64 addr, struct section *fault_sec)
{
    printk("Permission fault: pd=%p, addr=%p, fault_sec=%p\n", pd, (void *)addr, fault_sec);
    PTEntriesPtr pte = get_pte(pd, addr, false);
    ASSERT(pte != NULL && *pte != NULL);

    if ((*pte & PTE_USER) == 0) return -1;

    if ((*pte & PTE_RO) == PTE_RO)
    {
        if ((fault_sec->flags & ST_RO) == ST_RO) return -1;
        else 
        {
            void *new_pg = kalloc_page();
            void *old_pg = (void *)(P2K(*pte & ~(PAGE_SIZE - 1)));
            memcpy(new_pg, old_pg, PAGE_SIZE);
            *pte = (K2P(new_pg) | PTE_USER_DATA | PTE_RW);
        }
    }
    return 1;
}

int pgfault_handler(u64 iss)
{
    printk("Page fault: %llx\n", iss);
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 fault_addr = arch_get_far();
    struct section *fault_sec = lookup_section(pd, fault_addr);

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */
    if (fault_sec == NULL)
    {
        printk("Invalid address: %p\n", (void *)fault_addr);
        printk("Faulting instruction: %p\n", (void *)arch_get_elr());
        p->killed = true;
        return -1;
    }

    u64 fsc = iss & FAULT_STATUS_CODE_MASK;
    switch (fsc)
    {
        case ADDRESS_SIZE_FAULT_0:
        case ADDRESS_SIZE_FAULT_1:
        case ADDRESS_SIZE_FAULT_2:
        case ADDRESS_SIZE_FAULT_3:
            PANIC();
            break;
        case TRANSLATION_FAULT_0:
        case TRANSLATION_FAULT_1:
        case TRANSLATION_FAULT_2:
        case TRANSLATION_FAULT_3:
            handle_missing_pte(pd, fault_addr, fault_sec);
            break;
        case ACCESS_FLAG_FAULT_0:
        case ACCESS_FLAG_FAULT_1:
        case ACCESS_FLAG_FAULT_2:
        case ACCESS_FLAG_FAULT_3:
            PANIC();
            break;
        case PERMISSION_FAULT_0:
        case PERMISSION_FAULT_1:
        case PERMISSION_FAULT_2:
        case PERMISSION_FAULT_3:
            if (handle_permission_fault(pd, fault_addr, fault_sec) < 0)
            {
                p->killed = true;
                return -1;
            }
            break;
        default:
            PANIC();
    }

    arch_tlbi_vmalle1is();
    return 1;
    /* (Final) TODO END */
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */
    _for_in_list(node, from_head)
    {
        if (node == from_head) continue;
        struct section *from_sec = container_of(node, struct section, stnode);
        struct section *to_sec = (struct section*)kalloc(sizeof(struct section));
        if (!to_sec) PANIC();
        memcpy(to_sec, from_sec, sizeof(struct section));
        // if (from_sec->flags & ST_FILE)
        // {
        //     to_sec->fp = from_sec->fp;
        //     to_sec->offset = from_sec->offset;
        //     to_sec->length = from_sec->length;
        // }
        _insert_into_list(to_head, &to_sec->stnode);
    }
    /* (Final) TODO END */
}
