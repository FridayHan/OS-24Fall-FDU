#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

void* alloc_page_table() {
    // 调用kalloc_page分配一页内存
    void* page_table = kalloc_page();
    if (page_table) {
        // 初始化为0，清除旧数据
        memset(page_table, 0, PAGE_SIZE);
    }
    return page_table;  // 返回页表指针
}

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    // 获取各级页表索引
    int l1_idx = (va >> 39) & 0x1FF;
    int l2_idx = (va >> 30) & 0x1FF;
    int l3_idx = (va >> 21) & 0x1FF;
    int l4_idx = (va >> 12) & 0x1FF;

    // 当前页表指针，初始化为指向第一级页表
    PTEntriesPtr current_pt = pgdir->pt;

    if (!current_pt) {
        if (!alloc) return NULL; // 如果页表不存在且不需要分配，直接返回NULL
        current_pt = alloc_page_table(); // 分配第一级页表
        if (!current_pt) return NULL;
        pgdir->pt = (PTEntriesPtr *)P2V(current_pt); // 页表物理地址转换为虚拟地址
    }

    // 遍历各级页表
    for (int i = 0; i < 3; i++) {
        int idx = (i == 0) ? l1_idx : (i == 1) ? l2_idx : l3_idx;

        // 计算指向当前页表项的指针
        PTEntriesPtr entry = current_pt + idx;

        // 检查当前级别页表项是否存在且有效
        if (!(*entry & PTE_PRESENT)) {
            if (!alloc) return NULL; // 如果不允许分配，返回NULL

            // 分配新的下一级页表
            PTEntriesPtr new_table = alloc_page_table();
            if (!new_table) return NULL;

            // 将新页表的物理地址存入当前页表项
            *entry = V2P(new_table) | PTE_PRESENT;
        }

        // 进入下一级页表（物理地址转换为虚拟地址）
        current_pt = (PTEntriesPtr *)P2V(*entry & ~PTE_FLAGS_MASK); // 清除标志位，获取物理地址
    }

    // 返回最后一级页表中的PTE指针
    return &current_pt[l4_idx];
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
}

void free_page_table(PTEntriesPtr *pt, int level) {
    if (!pt) return; // 如果页表指针为空，直接返回

    if (level == 4) {
        // 如果已经到了第四级页表，释放它
        kfree_page(pt); // 假设 kfree_page 释放一页内存
        return;
    }

    // 遍历当前级别的页表项
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (pt[i] & PTE_PRESENT) {
            // 获取下一级页表的地址
            PTEntriesPtr *next_pt = (PTEntriesPtr *)P2V(pt[i] & ~PTE_FLAGS_MASK);
            // 递归释放下一级页表
            free_page_table(next_pt, level + 1);
        }
    }

    // 释放当前页表本身
    kfree_page(pt); // 假设 kfree_page 释放一页内存
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE

    if (pgdir->pt) {
        free_page_table(pgdir->pt, 1); // 从第一级页表开始递归释放
        pgdir->pt = NULL; // 清除页表指针
    }
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}
