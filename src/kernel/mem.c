#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <common/defines.h>
#include <kernel/printk.h>

#define BLOCK_SIZE 32
#define BLOCKS_PER_PAGE (PAGE_SIZE / BLOCK_SIZE)

RefCount kalloc_page_cnt;

typedef struct Page {
    ListNode list;
    void *addr;
    unsigned char bitmap[BLOCKS_PER_PAGE / 8];
    SpinLock lock;
} Page;

ListNode free_pages;
ListNode used_pages;
SpinLock free_pages_lock;
SpinLock used_pages_lock;
extern char end[];

void my_memset(void *s, int c, unsigned long long n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
}

void kinit()
{
    init_rc(&kalloc_page_cnt);

    init_spinlock(&free_pages_lock);
    init_spinlock(&used_pages_lock);

    free_pages.next = NULL;
    used_pages.next = NULL;

    unsigned long long start_phys = round_up(K2P_WO((unsigned long long)end), PAGE_SIZE);
    unsigned long long end_phys = PHYSTOP;

    printk("kinit: end = %llx, physical start = %llx, PHYSTOP = %llx\n", (unsigned long long)end, start_phys, end_phys);

    for (unsigned long addr = (unsigned long)start_phys; addr < PHYSTOP; addr += PAGE_SIZE) {
        void* page_addr = (void*)P2K_WO(addr);
        Page *page = (Page *)page_addr;
        init_spinlock(&page->lock);

        my_memset(page->bitmap, 0, sizeof(page->bitmap));

        page->list.next = free_pages.next;
        free_pages.next = &page->list;

        page->addr = page_addr;

        // printk("kinit: Added free page %p (physical: %p)\n", page, (void*)addr);
    }
    printk("kinit: Total free pages initialized\n");
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);

    acquire_spinlock(&free_pages_lock);
    printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

    if (free_pages.next != NULL) {
        Page* page = container_of(free_pages.next, Page, list);
        free_pages.next = page->list.next;
        page->list.next = NULL;
        
        page->list.next = used_pages.next;
        used_pages.next = &page->list;

        printk("kalloc_page: Allocated page %p\n", page);

        release_spinlock(&free_pages_lock);
        printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

        return page->addr;
    }

    release_spinlock(&free_pages_lock);
    printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

    printk("kalloc_page: No free pages available\n");
    return NULL;
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);

    acquire_spinlock(&used_pages_lock);
    printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

    if (p != NULL) {
        Page *page = (Page *)p;

        ListNode* prev = &used_pages;
        ListNode* current = used_pages.next;
        while (current != NULL) {
            if (current == page->list.next) {
                prev->next = page->list.next;
                break;
            }
            prev = current;
            current = current->next;
        }

        page->list.next = free_pages.next;
        free_pages.next = &page->list;
    }

    release_spinlock(&free_pages_lock);
    printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

    printk("kfree_page: Freed page %p\n", p);

    return;
}

void *kalloc(unsigned long long size)
{
    if (size == 0 || size > (PAGE_SIZE / 2)) return NULL;

    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;


    static SpinLock allocator_lock = {0};
    init_spinlock(&allocator_lock);
    acquire_spinlock(&allocator_lock);
    printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

    Page* target_page = NULL;
    int block_start = -1;

    acquire_spinlock(&used_pages_lock);
    printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

    ListNode* current_node = used_pages.next;
    while (current_node != NULL) {
        Page* current = container_of(current_node, Page, list);

        // 遍历位图，寻找连续的空闲块
        int consecutive = 0;
        int start = -1;
        for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
            int byte = i / 8;
            int bit = i % 8;
            if (!(current->bitmap[byte] & (1 << bit))) {
                if (consecutive == 0) start = i;
                consecutive++;
                if (consecutive == blocks_needed) {
                    block_start = start;
                    break;
                }
            }
            else {
                consecutive = 0;
            }
        }

        if (block_start != -1) {
            // 标记块为已分配
            for (int i = block_start; i < block_start + blocks_needed; i++) {
                int byte = i / 8;
                int bit = i % 8;
                current->bitmap[byte] |= (1 << bit);
            }
            target_page = current;
            break;
        }

        current_node = current_node->next;
    }
    release_spinlock(&used_pages_lock);
    printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

    if (target_page == NULL) {
        // 没有找到合适的块，尝试分配新页
        target_page = (Page*)kalloc_page();
        if (target_page == NULL) {
            // release_spinlock(&allocator_lock);
            printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);
            return NULL;
        }

        // 标记所需块为已分配
        for (int i = 0; i < blocks_needed; i++) {
            int byte = i / 8;
            int bit = i % 8;
            target_page->bitmap[byte] |= (1 << bit);
        }
        block_start = 0;
    }

    unsigned long long page_addr = (unsigned long long)target_page;
    unsigned long long block_addr = page_addr + block_start * BLOCK_SIZE;

    release_spinlock(&allocator_lock);
    printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

    printk("kalloc: Allocated %llu bytes at %llx (page: %p, block_start: %d)\n",
           size, block_addr, target_page, block_start);
    return (void*)block_addr;

    // unsigned long long log_2_blockSize = round(size)
    // if (log_2_blockSize> 0 && log_2_blockSize <= PAGE_SIZE / 2 && free_list_head != NULL)
    // {
    //     if (!used_Blocklist[log_2_blockSize].is_full) {
    //         used_Blocklist[log_2_blockSize].used_list_head
    //     }
    //     else {
    //         void* addr = kalloc_page();
    //     }
    // }
    return NULL;
}

void kfree(void *ptr)
{
    if (ptr == NULL) {
        printk("kfree: NULL pointer\n");
        return;
    }

    unsigned long long addr = (unsigned long long)ptr;
    unsigned long long page_addr = addr & ~(PAGE_SIZE - 1);
    Page* page = (Page*)P2K_WO(page_addr);
    int block_index = (addr - page_addr) / BLOCK_SIZE;

    if (block_index < 0 || block_index >= BLOCKS_PER_PAGE) {
        printk("kfree: Invalid block index %d for ptr %p\n", block_index, ptr);
        return;
    }

    static SpinLock allocator_lock = {0};
    init_spinlock(&allocator_lock);
    acquire_spinlock(&allocator_lock);
    printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

    int byte = block_index / 8;
    int bit = block_index % 8;
    page->bitmap[byte] &= ~(1 << bit);

    bool all_free = true;
    for (int i = 0; i < (BLOCKS_PER_PAGE / 8); i++) {
        if (page->bitmap[i] != 0) {
            all_free = false;
            break;
        }
    }

    if (all_free) {
        acquire_spinlock(&used_pages_lock);
        printk("Trying to acquire lock at %s:%d\n", __FILE__, __LINE__);

        ListNode* prev = &used_pages;
        ListNode* current = used_pages.next;
        while (current != NULL) {
            if (current == page->list.next) {
                prev->next = page->list.next;
                break;
            }
            prev = current;
            current = current->next;
        }

        page->list.next = free_pages.next;
        free_pages.next = page->list.next;
        release_spinlock(&used_pages_lock);
        printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);
    }
    
    release_spinlock(&allocator_lock);
    printk("Trying to release lock at %s:%d\n", __FILE__, __LINE__);

    printk("kfree: Freed block %d at %p (page: %p)\n", block_index, ptr, page);
    return;
}
