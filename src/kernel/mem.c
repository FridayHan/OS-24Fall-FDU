#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>

RefCount kalloc_page_cnt;

static PagedAllocator allocator;

static Page* free_pages;
static SpinLock free_pages_lock;

void kinit()
{
    init_rc(&kalloc_page_cnt);

    init_spinlock(&free_pages_lock);
    printk("kinit: acquiring free_pages_lock\n");
    acquire_spinlock(&free_pages_lock);

    for (int i = 0; i < MAX_SIZE_CLASS; i++) {
        allocator.free_pages[i] = NULL;
        init_spinlock(&allocator.locks[i]);
    }
    
    u64 start_addr = round_up(K2P((u64)end), PAGE_SIZE);
    printk("kinit: Initializing Paged Allocator. Starting from physical address %p. Free pages: %llu\n", (void*)start_addr, (PHYSTOP - start_addr) / PAGE_SIZE);

    for (u64 addr = start_addr; addr < PHYSTOP; addr += PAGE_SIZE) {
        Page* new_page = (Page*)addr;
        // init_spinlock(&new_page->lock);
        new_page->free_list_num = 0;
        new_page->block_size = 0;
        new_page->free_list = NULL;
        new_page->next = free_pages;
        free_pages = new_page;
    }
    printk("kinit: releasing free_pages_lock\n");
    release_spinlock(&free_pages_lock);

    printk("Paged Allocator initialized.\n");
}

void kinit_page(Page* page, u64 block_size)
{
    u64 size_class = get_size_class(block_size);
    // printk("kinit_page: acquiring lock for size class %llu\n", size_class);
    // acquire_spinlock(&allocator.locks[size_class]);

    page->free_list_num = (PAGE_SIZE - align_size(sizeof(Page))) / block_size;
    printk("kinit_page: Initializing page %p with block size %llu. %u blocks.\n", page, block_size, page->free_list_num);
    // printk("align_size(sizeof(Page)) = %llu\n", (u64)(sizeof(Page)));
    page->block_size = block_size;
    page->free_list = NULL;
    page->next = NULL;

    // 初始化页内的自由块链表
    char* block_ptr = (char*)round_up((u64)((char*)page + align_size(sizeof(Page))), block_size);
    u64 num_blocks = (PAGE_SIZE - round_up(align_size(sizeof(Page)), block_size)) / block_size;
    printk("kinit_page: block_ptr = %p, num_blocks = %llu, size_class = %llu\n", block_ptr, num_blocks, size_class);

    for (u64 i = 0; i < num_blocks; i++) {
        *((void**)block_ptr) = page->free_list;
        page->free_list = block_ptr;
        block_ptr += block_size;
    }
    
    // 将新页插入到对应大小类别的自由列表
    page->next = allocator.free_pages[size_class];
    allocator.free_pages[size_class] = page;

    // printk("kinit_page: releasing lock for size class %llu\n", size_class);
    // release_spinlock(&allocator.locks[size_class]);

    printk("allocate_new_page: Allocated new page %p for size class %llu bytes with %llu blocks.\n",
           page, block_size, num_blocks);

    return;
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);
    // printk("kalloc_page: acquiring free_pages_lock\n");
    acquire_spinlock(&free_pages_lock);

    if (free_pages == NULL) {
        printk("kalloc_page: Out of memory.\n");
        return NULL;
    }
    
    Page* allocated_page = free_pages;
    free_pages = free_pages->next;
    allocated_page->next = NULL;

    // printk("kalloc_page: Allocated page %p.\n", allocated_page);

    // printk("kalloc_page: releasing free_pages_lock\n");
    release_spinlock(&free_pages_lock);
    return (void*)P2K(allocated_page);
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);
    // printk("kfree_page: acquiring free_pages_lock\n");

    acquire_spinlock(&free_pages_lock);

    if ((u64)p % PAGE_SIZE != 0) {
        printk("kfree_page: Attempted to free a non-page-aligned pointer.\n");
        return;
    }

    Page* page = (Page*)K2P((u64)p);
    page->next = free_pages;
    free_pages = page;

    // printk("kfree_page: Freed page %p.\n", page);

    // printk("kfree_page: releasing free_pages_lock\n");
    release_spinlock(&free_pages_lock);
    return;
}

u64 align_size(u64 size) {
    // if (size <= 2)
    //     return round_up(size, 2);
    // else if (size <= 4)
    //     return round_up(size, 4);
    // else 
        return round_up(size, 8);
}

int get_size_class(u64 x) {
    return 61 - __builtin_clzll(x - 1);
}

void *kalloc(u64 size)
{
    if (size == 0 || size > PAGE_SIZE / 2) {
        printk("kalloc error: size error. Requested size: %llu.\n", size);
        return NULL;
    }

    u64 aligned_size = align_size(size);
    int size_class = get_size_class(aligned_size);
    aligned_size = size_classes[size_class];
    printk("kalloc: Requested size: %llu. Aligned size: %llu. Size class: %d\n", size, aligned_size, size_class);
    // printk("kalloc: acquiring lock for size class %d\n", size_class);
    acquire_spinlock(&allocator.locks[size_class]);

    if (size_class == -1) {
        printk("kalloc: No suitable size class for size %llu.\n", aligned_size);
        return NULL;
    }

    Page* page = allocator.free_pages[size_class];
    void* block = NULL;

    while (page) {
        // acquire_spinlock(&page->lock);
        if (page->free_list) {
            block = page->free_list;
            page->free_list = *((void**)page->free_list);
            // release_spinlock(&page->lock);
            break;
        }
        page = page->next;
        // release_spinlock(&page->lock);
    }

    if (!block) {
        page = kalloc_page();

        if (!page) {
            // printk("kalloc: releasing lock for size class %d\n", size_class);
            release_spinlock(&allocator.locks[size_class]);
            return NULL;
        }

        kinit_page(page, aligned_size);
        // acquire_spinlock(&page->lock);
        block = page->free_list;
        page->free_list = *((void**)page->free_list);
        // release_spinlock(&page->lock);
        // printk("kalloc: releasing lock for size class %d\n", size_class);
        release_spinlock(&allocator.locks[size_class]);
        return block;
    }

    page->free_list_num--;
    // printk("kalloc: releasing lock for size class %d\n", size_class);
    release_spinlock(&allocator.locks[size_class]);

    return block;
}

void kfree(void *ptr)
{
    u64 paddr = K2P((u64)ptr);
    u64 page_paddr = paddr & ~(PAGE_SIZE - 1);
    void* page_virtual = (void*)P2K(page_paddr);
    Page* page = (Page*)page_virtual;
    u64 block_size = page->block_size;
    int size_class = get_size_class(block_size);

    // printk("kfree: acquiring lock for size class %d\n", size_class);
    acquire_spinlock(&allocator.locks[size_class]);

    if (!ptr) {
        printk("kfree: Attempted to free a NULL pointer.\n");
        return;
    }

    // printk("kfree: Freeing block %p. Block size: %llu. Size class: %d\n", ptr, block_size, size_class);

    // acquire_spinlock(&page->lock);

    *((void**)ptr) = page->free_list;
    page->free_list = ptr;

    page->free_list_num++;

    if ((u64)page->free_list_num == (PAGE_SIZE - align_size(sizeof(Page))) / block_size) {
        allocator.free_pages[size_class] = page->next;
        // release_spinlock(&page->lock);
        kfree_page(page);
        // return;
    }
    // release_spinlock(&page->lock);
    // printk("kfree: releasing lock for size class %d\n", size_class);
    release_spinlock(&allocator.locks[size_class]);
    return;
}
