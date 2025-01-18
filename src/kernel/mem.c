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

static Page* free_pages = NULL;
static SpinLock free_pages_lock;

u16 align_size(u16 size) {
    for (int i = 0; i < MAX_SIZE_CLASS; i++) {
        if (size <= size_classes[i]) {
            return size_classes[i];
        }
    }
    // 默认对齐到最大的大小类别
    return size_classes[MAX_SIZE_CLASS - 1];
    // 简洁但可维护性差的实现
    // return round_up(size, 8);
}

int get_size_class(u16 size) {
    for (int i = 0; i < MAX_SIZE_CLASS; i++) {
        if (size <= size_classes[i]) {
            return i;
        }
    }
    return -1;
    // 简洁但可维护性差的实现
    // if (size <= 8)
    //     return 0;
    // return 61 - __builtin_clzll(size - 1);
}

void kinit()
{
    init_rc(&kalloc_page_cnt);
    init_spinlock(&free_pages_lock);
    acquire_spinlock(&free_pages_lock);

    // 初始化allocator
    for (int i = 0; i < MAX_SIZE_CLASS; i++) {
        allocator.free_pages[i] = NULL;
        init_spinlock(&allocator.locks[i]);
    }
    
    // 初始化free_pages
    u64 start_addr = (round_up((u64)end, PAGE_SIZE)); // 可分配物理页的起始地址
    for (u64 addr = start_addr; addr < P2K(PHYSTOP); addr += PAGE_SIZE) {
        Page* new_page = (Page*)addr;
        new_page->free_list_num = 0;
        new_page->block_size = 0;
        new_page->free_list_offset = 0;
        // 将新页插入到free_pages
        new_page->next = free_pages;
        free_pages = new_page;
    }

    release_spinlock(&free_pages_lock);
    return;
}

void kinit_page(Page* page, u16 block_size)
{
    // ERROR: 检查页是否为空
    if (!page) {
        printk("kinit_page: Attempted to initialize a NULL page.\n");
        return;
    }

    int size_class = get_size_class(block_size);
    page->free_list_num = (USABLE_PAGE_SIZE(block_size)) / block_size;
    page->block_size = block_size;
    page->free_list_offset = 0;
    page->next = NULL;

    // 初始化页内的自由块链表
    u16 block_offset = (u16)round_up((u64)(sizeof(Page)), (u64)block_size);
    for (int i = 0; i < page->free_list_num; i++) {
        *(u16*)((char*)page + block_offset) = page->free_list_offset;
        page->free_list_offset = (u16)block_offset;
        block_offset += block_size;
    }
    
    // 将新页插入到对应大小类别的自由列表
    page->prev = NULL;
    page->next = allocator.free_pages[size_class];
    if (allocator.free_pages[size_class]) {
        allocator.free_pages[size_class]->prev = page;
    }
    allocator.free_pages[size_class] = page;
    page->in_free_list = true;
    return;
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);
    acquire_spinlock(&free_pages_lock);

    // ERROR: 检查是否有空闲页
    if (free_pages == NULL) {
        printk("kalloc_page: Out of memory.\n");
        return NULL;
    }
    
    // 取出一个空闲物理页
    Page* allocated_page = free_pages;
    free_pages = free_pages->next;
    allocated_page->next = NULL;

    release_spinlock(&free_pages_lock);
    return (void*)allocated_page;
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);
    acquire_spinlock(&free_pages_lock);

    // ERROR: 检查释放的页是否合法
    if ((u64)p % PAGE_SIZE != 0) {
        printk("kfree_page: Attempted to free a non-page-aligned pointer.\n");
        return;
    }

    // 将页插入到空闲页链表
    Page* page = (Page*)(u64)p;
    page->next = free_pages;
    free_pages = page;

    release_spinlock(&free_pages_lock);
    return;
}

void *kalloc(u16 size)
{
    // ERROR: 检查请求的大小是否合法
    if (size == 0 || size > PAGE_SIZE / 2) {
        printk("kalloc error: size error. Requested size: %u.\n", size);
        return NULL;
    }

    // 对齐请求的大小
    u16 aligned_size = align_size(size);
    int size_class = get_size_class(aligned_size);

    acquire_spinlock(&allocator.locks[size_class]);

    // ERROR: 没有合适的大小类别
    if (size_class == -1) {
        printk("kalloc: No suitable size class for size %u.\n", aligned_size);
        return NULL;
    }

    // 查找合适的空闲块
    Page* page = allocator.free_pages[size_class];

    if (!page) {
        page = kalloc_page();
        // ERROR: 检查是否分配成功
        if (!page) {
            release_spinlock(&allocator.locks[size_class]);
            printk("kalloc: Out of memory.\n");
            return NULL;
        }
        kinit_page(page, aligned_size);
    }

    u16 block_offset = page->free_list_offset;
    page->free_list_offset = *(u16*)((char*)page + block_offset);
    page->free_list_num--;

    if (page->free_list_num == 0) {
        if (page->prev) {
            page->prev->next = page->next;
        } else {
            allocator.free_pages[size_class] = page->next;
        }
        if (page->next) {
            page->next->prev = page->prev;
        }
        page->in_free_list = false;
        page->next = NULL;
        page->prev = NULL;
    }

    release_spinlock(&allocator.locks[size_class]);
    return (Page*)((char*)page + block_offset);
}

void kfree(void *ptr)
{
    // ERROR: 检查释放的指针是否合法
    if (!ptr) {
        printk("kfree: Attempted to free a NULL pointer.\n");
        return;
    }

    // u64 phys_addr = K2P((u64)ptr);
    // u64 page_phys_addr = phys_addr & ~(PAGE_SIZE - 1);

    void* page_virt_addr = (void*)((u64)ptr & ~(PAGE_SIZE - 1));
    Page* page = (Page*)page_virt_addr;
    u16 block_size = page->block_size;
    int size_class = get_size_class(block_size);

    // ERROR: 检查释放的块是否合法
    if (size_class == -1) {
        printk("kfree: Invalid block size %u.\n", block_size);
        return;
    }

    acquire_spinlock(&allocator.locks[size_class]);

    bool was_full = (page->free_list_num == 0);

    *(u16*)ptr = page->free_list_offset;
    page->free_list_offset = (u16)((char*)ptr - (char*)page);
    page->free_list_num++;

    // 若页变free，则插入到allocator.free_pages
    if (was_full) {
        page->next = allocator.free_pages[size_class];
        page->prev = NULL;
        if (allocator.free_pages[size_class]) {
            allocator.free_pages[size_class]->prev = page;
        }
        allocator.free_pages[size_class] = page;
        page->in_free_list = true;
    }

    // 若页已完全free，释放页
    if ((u64)page->free_list_num == (USABLE_PAGE_SIZE(block_size)) / block_size) {
        if (page->in_free_list) {
            if (page->prev) {
                page->prev->next = page->next;
            } else {
                allocator.free_pages[size_class] = page->next;
            }
            if (page->next) {
                page->next->prev = page->prev;
            }
            page->in_free_list = false;
        }
        page->next = NULL;
        page->prev = NULL;
        kfree_page(page);
    }

    release_spinlock(&allocator.locks[size_class]);
    return;
}

void* get_zero_page() {
    /* (Final) TODO BEGIN */

    /* (Final) TODO END */
    return NULL;
}
