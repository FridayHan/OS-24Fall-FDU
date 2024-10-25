#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    // 分配根页表
    if (!pgdir->pt) {
        if (!alloc) return NULL;
        pgdir->pt = (PTEntriesPtr)kalloc_page();
        if (!pgdir->pt) return NULL;
        memset(pgdir->pt, 0, PAGE_SIZE);
    }

    PTEntriesPtr table = pgdir->pt;
    u64 idx;

    // 第1级页表（PGD），获取索引并查找
    idx = VA_PART0(va);
    if (!(table[idx] & PTE_VALID)) {
        if (!alloc) return NULL;
        // 分配第2级页表
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;  // 分配失败
        void* table_address = (void*)P2K(PTE_ADDRESS(table[idx]));
        memset(table_address, 0, PAGE_SIZE);  // 初始化为0
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    // 第2级页表
    idx = VA_PART1(va);
    if (!(table[idx] & PTE_VALID)) {
        if (!alloc) return NULL;
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;  // 分配失败
        memset((void*)P2K(PTE_ADDRESS(table[idx])), 0, PAGE_SIZE);
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    // 第3级页表
    idx = VA_PART2(va);
    if (!(table[idx] & PTE_VALID)) {
        if (!alloc) return NULL;
        table[idx] = (u64)K2P(kalloc_page()) | PTE_TABLE | PTE_VALID;
        if (!(table[idx] & PTE_VALID)) return NULL;  // 分配失败
        memset((void*)P2K(PTE_ADDRESS(table[idx])), 0, PAGE_SIZE);
    }
    table = (PTEntriesPtr)P2K(PTE_ADDRESS(table[idx]));

    // 第4级页表
    idx = VA_PART3(va);
    if (!(table[idx] & PTE_VALID)) {
        if (!alloc) return NULL;
    //     u64 phys_page = (u64)kalloc_page();  // 分配物理页面
    //     if (!phys_page) return NULL;  // 分配失败
    //     table[idx] = phys_page | PTE_PAGE | PTE_VALID | PTE_RW;  // 设置页表项为有效并映射物理页面
    }

    // 返回指向该页表项的指针
    return &table[idx];
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE

    if (!pgdir->pt) return;  // 如果根页表不存在，直接返回

    // 遍历并释放各级页表
    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        if (pgdir->pt[i] & PTE_VALID) {
            PTEntriesPtr table1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgdir->pt[i]));

            // 第1级页表
            for (int j = 0; j < N_PTE_PER_TABLE; j++) {
                if (table1[j] & PTE_VALID) {
                    PTEntriesPtr table2 = (PTEntriesPtr)P2K(PTE_ADDRESS(table1[j]));

                    // 第2级页表
                    for (int k = 0; k < N_PTE_PER_TABLE; k++) {
                        if (table2[k] & PTE_VALID) {
                            PTEntriesPtr table3 = (PTEntriesPtr)P2K(PTE_ADDRESS(table2[k]));

                            // 第3级页表
                            for (int l = 0; l < N_PTE_PER_TABLE; l++) {
                                if (table3[l] & PTE_VALID) {
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
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}
