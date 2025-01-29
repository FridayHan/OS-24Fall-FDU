#pragma once

#include <aarch64/mmu.h>
#include <common/spinlock.h>
#include <common/list.h>

typedef struct pgdir {
    PTEntriesPtr pt;
    SpinLock lock;
    ListNode section_head;
} Pgdir;

void init_pgdir(Pgdir *pgdir);
WARN_RESULT PTEntriesPtr get_pte(Pgdir *pgdir, u64 va, bool alloc);
void free_pgdir(Pgdir *pgdir);
void attach_pgdir(Pgdir *pgdir);
void vmmap(Pgdir *pd, u64 va, void *ka, u64 flags);
int copyout(Pgdir *pd, void *va, void *p, usize len);