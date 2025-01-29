#pragma once
#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/rc.h>
#include <common/rc.h>
#include <common/list.h>
#include <common/string.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

#define PAGE_COUNT ((P2K(PHYSTOP) - PAGE_BASE((u64) & end)) / PAGE_SIZE - 2)
#define MAX_SIZE_CLASS 35
#define USABLE_PAGE_SIZE(block_size) (PAGE_SIZE - round_up((u64)sizeof(Page), (u64)block_size))

static const unsigned long long size_classes[MAX_SIZE_CLASS] = {8, 12, 16, 24, 32, 40, 48, 56, 64, 80, 96, 128, 160, 192, 216, 232, 256, 320, 352, 384, 448, 512, 768, 904, 1024, 1200, 1320, 1520, 1640, 1720, 2048};
extern char end[];

typedef struct Page {
    struct Page* prev;
    struct Page* next;
    int free_list_num;
    unsigned long long block_size;
    unsigned long long free_list_offset;
    bool in_free_list;
    // int cnt;
    RefCount rc;
} Page;

typedef struct {
    Page* free_pages[MAX_SIZE_CLASS];
    SpinLock locks[MAX_SIZE_CLASS];
} PagedAllocator;

void kinit_page(Page*, unsigned long long);
unsigned long long align_size(unsigned long long);
int get_size_class(unsigned long long);

void kinit();
u64 left_page_cnt();

WARN_RESULT void *kalloc_page();
void kfree_page(void *);

WARN_RESULT void *kalloc(unsigned long long);
void kfree(void *);

WARN_RESULT void *get_zero_page();
void kshare_page(u64 addr);
