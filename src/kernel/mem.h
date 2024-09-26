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
#define USABLE_PAGE_SIZE(block_size) (PAGE_SIZE - round_up((u64)sizeof(Page), (u64)block_size))

extern char end[];

typedef struct Page {
    // SpinLock lock;
    int free_list_num;
    u16 block_size;
    u16 free_list_offset;
    struct Page* next;
} Page;

typedef struct {
    Page* free_pages[MAX_SIZE_CLASS];
    SpinLock locks[MAX_SIZE_CLASS];
} PagedAllocator;

static const u16 size_classes[MAX_SIZE_CLASS] = {
    4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048
};

void kinit_page(Page*, u16);
u16 align_size(u16);
int get_size_class(u16);

void kinit();
void* kalloc_page();
void kfree_page(void*);
void* kalloc(u16);
void kfree(void*);
