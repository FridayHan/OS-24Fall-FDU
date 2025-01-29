#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <common/defines.h>
#include <kernel/printk.h>
#include <kernel/paging.h>

PTEntriesPtr get_pte(Pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    PTEntriesPtr table = pgdir->pt;
    if (!table)
    {
        if (!alloc) return NULL;
        table = (PTEntriesPtr)kalloc_page();
        if (!table) return NULL;
        memset(table, 0, PAGE_SIZE);
        pgdir->pt = table;
    }

    u64 idx = VA_PART0(va);
    if (!(table[idx] & PTE_VALID))
    {
        if (!alloc) return NULL;
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;
        void* table_address = (void*)P2K(PTE_ADDRESS(table[idx]));
        memset(table_address, 0, PAGE_SIZE);
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    idx = VA_PART1(va);
    if (!(table[idx] & PTE_VALID))
    {
        if (!alloc) return NULL;
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;
        memset((void*)P2K(PTE_ADDRESS(table[idx])), 0, PAGE_SIZE);
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    idx = VA_PART2(va);
    if (!(table[idx] & PTE_VALID))
    {
        if (!alloc) return NULL;
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;
        memset((void*)P2K(PTE_ADDRESS(table[idx])), 0, PAGE_SIZE);
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    idx = VA_PART3(va);
    if (!(table[idx] & PTE_VALID))
    {
        if (!alloc) return NULL;
        // u64 phys_page = (u64)kalloc_page();
        // if (!phys_page) return NULL;
        // table[idx] = phys_page | PTE_PAGE | PTE_VALID | PTE_RW;
    }

    return &table[idx];
}

void init_pgdir(Pgdir *pgdir)
{
    pgdir->pt = NULL;
    init_spinlock(&pgdir->lock);
    init_sections(&pgdir->section_head);
}

void free_pgdir(Pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE

    if (!pgdir->pt) return;

    for (int i = 0; i < N_PTE_PER_TABLE; i++)
    {
        if (pgdir->pt[i] & PTE_VALID)
        {
            PTEntriesPtr table1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgdir->pt[i]));
            // 第1级页表
            for (int j = 0; j < N_PTE_PER_TABLE; j++)
            {
                if (table1[j] & PTE_VALID)
                {
                    PTEntriesPtr table2 = (PTEntriesPtr)P2K(PTE_ADDRESS(table1[j]));
                    // 第2级页表
                    for (int k = 0; k < N_PTE_PER_TABLE; k++)
                    {
                        if (table2[k] & PTE_VALID)
                        {
                            PTEntriesPtr table3 = (PTEntriesPtr)P2K(PTE_ADDRESS(table2[k]));
                            // 第3级页表
                            for (int l = 0; l < N_PTE_PER_TABLE; l++)
                            {
                                if (table3[l] & PTE_VALID)
                                {
                                    // 第3级页表项有效，不释放物理页，只释放页表本身
                                    table3[l] = 0;
                                }
                            }
                            // 释放第3级页表
                            kfree_page(table3);
                        }
                    }
                    // 释放第2级页表
                    kfree_page(table2);
                }
            }
            // 释放第1级页表
            kfree_page(table1);
        }
    }

    // 释放根页表
    kfree_page(pgdir->pt);
    pgdir->pt = NULL;

    free_sections(pgdir);
}

void attach_pgdir(Pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(Pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */
    u64 pa = (u64)K2P(ka);
    PTEntriesPtr pte = get_pte(pd, va, true);
    if (!pte)
    {
        printk("vmmap: get_pte failed\n");
        return;
    }
    *pte = PAGE_BASE(pa) | flags;
    arch_tlbi_vmalle1is();
    /* (Final) TODO END */
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(Pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    usize total_copied = 0;
    while (total_copied < len)
    {
        PTEntriesPtr pte = get_pte(pd, (u64)va, true);
        if (*pte == NULL)
        {
            void *new_page = kalloc_page();
            *pte = K2P(new_page) | PTE_USER_DATA;
        }

        usize copy_size = MIN(len - total_copied, PAGE_SIZE - VA_OFFSET(va));
        void *dst = (void *)(P2K(PTE_ADDRESS(*pte)) + VA_OFFSET(va));
        memcpy(dst, p, copy_size);

        total_copied += copy_size;
        p += copy_size;
        va += copy_size;
    }
    if (total_copied == len)
        return 0;
    else
        return -1;
    /* (Final) TODO END */
}
