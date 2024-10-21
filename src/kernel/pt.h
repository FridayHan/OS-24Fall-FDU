#pragma once

#include <aarch64/mmu.h>

#define PAGE_TABLE_LEVELS 4
#define ENTRY_COUNT 512
#define PTE_PRESENT 0x1
#define PTE_FLAGS_MASK 0xFFF // 低12位用于标志位的掩码

struct pgdir {
    PTEntriesPtr pt;
};

void init_pgdir(struct pgdir *pgdir);
void free_pgdir(struct pgdir *pgdir);
void attach_pgdir(struct pgdir *pgdir);
