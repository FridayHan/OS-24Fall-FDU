#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>

RefCount kalloc_page_cnt;

extern char end[];

// typedef struct Page {
//     QueueNode node;
//     SpinLock lock;
// } Page;

typedef struct {
    QueueNode* free_list;
    SpinLock lock;
} PagedAllocator;

PagedAllocator allocator;

void kinit()
{
    init_rc(&kalloc_page_cnt);

    allocator.free_list = NULL;
    init_spinlock(&allocator.lock);
    
    unsigned long long addr = round_up(K2P((unsigned long long)end), PAGE_SIZE);
    printk("kinit: Initializing Paged Allocator. Starting from physical address %p. Free pages: %llu\n", (void*)addr, (PHYSTOP - addr) / PAGE_SIZE);
    for (; addr < PHYSTOP; addr += PAGE_SIZE) {
        QueueNode* new_page = (QueueNode*)addr;
        new_page->next = allocator.free_list;
        allocator.free_list = new_page;
    }

    printk("Paged Allocator initialized.\n");
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);

    acquire_spinlock(&allocator.lock);

    if (allocator.free_list == NULL) {
        release_spinlock(&allocator.lock);

        return NULL;
    }

    QueueNode* allocated_page = allocator.free_list;
    allocator.free_list = allocated_page->next;

    release_spinlock(&allocator.lock);

    return (void*)P2K((unsigned long long)allocated_page);
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);

    QueueNode* page = (QueueNode*)K2P((unsigned long long)p);
    acquire_spinlock(&allocator.lock);

    page->next = allocator.free_list;
    allocator.free_list = page;
    release_spinlock(&allocator.lock);

}

void *kalloc(unsigned long long size)
{
    if (size > PAGE_SIZE) {
        printk("kalloc: Requested size %llu exceeds PAGE_SIZE.\n", size);
        return NULL;
    }

    return kalloc_page();
}

void kfree(void *ptr)
{
    if (!ptr) {
        printk("kfree: Attempted to free a NULL pointer.\n");
        return;
    }
    kfree_page(ptr);
}
