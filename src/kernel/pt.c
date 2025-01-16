#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>

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

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */

    /* (Final) TODO END */
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    // 遍历 len 字节，并逐页处理
    u64 va_end = (u64)va + len;  // 目标虚拟地址空间的结束位置
    u64 offset = (u64)va % PAGE_SIZE;  // 当前页的偏移量
    u64 start_va = (u64)va;  // 开始虚拟地址
    u64 start_pa = (u64)p;  // 源内存地址

    // 每次处理一个完整的页
    while (start_va < va_end) {
        u64 va_page = start_va & ~(PAGE_SIZE - 1);  // 获取当前虚拟页的起始地址
        u64 pa_page = start_pa & ~(PAGE_SIZE - 1);  // 获取当前物理页的起始地址
        usize to_copy = PAGE_SIZE - (start_va - va_page);  // 计算剩余需要复制的字节数

        // 限制复制长度，确保不超过剩余的空间
        if (start_va + to_copy > va_end) {
            to_copy = va_end - start_va;
        }

        // 获取页表项
        PTEntriesPtr pte = get_pte(pd, va_page, true);  // 获取目标虚拟地址的页表项
        if (pte == NULL) {
            return -1;  // 如果页表项为 NULL，说明无法分配页
        }

        // 将源内存数据复制到目标虚拟地址
        memcpy((void *)P2K(*pte) + (start_va - va_page), (void *)(pa_page + (start_pa - va_page)), to_copy);

        // 更新地址
        start_va += to_copy;
        start_pa += to_copy;
    }

    return 0;  // 成功
    /* (Final) TODO END */
}