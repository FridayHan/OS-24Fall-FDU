#pragma once
#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>

#define MAX_SIZE_CLASS 10
#define USABLE_PAGE_SIZE(block_size) (PAGE_SIZE - round_up(sizeof(Page), block_size))

extern char end[];

typedef struct Page {
    // SpinLock lock;
    int free_list_num;
    u64 block_size;
    void* free_list;
    struct Page* next;
} Page;

typedef struct {
    Page* free_pages[MAX_SIZE_CLASS];
    SpinLock locks[MAX_SIZE_CLASS];
} PagedAllocator;

static const u64 size_classes[MAX_SIZE_CLASS] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

void kinit_page(Page*, u64);
u64 align_size(u64);
int get_size_class(u64);

void kinit();
void* kalloc_page();
void kfree_page(void*);
void* kalloc(u64);
void kfree(void*);
