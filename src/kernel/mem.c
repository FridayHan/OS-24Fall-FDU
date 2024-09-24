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
    unsigned char bitmap[BLOCKS_PER_PAGE / 8];
} Page;

ListNode free_pages;
ListNode used_pages;
extern char end[];

void kinit()
{
    init_rc(&kalloc_page_cnt);

    // 最好用static限制作用于再当前文件
    // KERNLINK + end

    free_pages.next = NULL;
    used_pages.next = NULL;

    unsigned long long start_phys = round_up(K2P_WO((unsigned long long)end), PAGE_SIZE);
    unsigned long long end_phys = PHYSTOP;

    for (unsigned long addr = (unsigned long)start_phys; addr < PHYSTOP; addr += PAGE_SIZE) {
        Page *page = (Page *)addr;
        page->addr = (void *)addr;
        page->next = free_list_head;
        free_list_head = page;
    }

    // static int page_id;
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);
    // 可用内存从end开始，但是end没有按照4k对齐
    if (free_list_head != NULL) {
        Page *page = free_list_head;
        free_list_head = free_list_head->next;
        return page->addr;
    }
    return NULL;
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);
    // 不能用递归
    if (p != NULL) {
        Page *page = (Page *)p;
        page->addr = (void *)p;
        page->next = free_list_head;
        free_list_head = page;
    }
    return;
}

unsigned long long round(unsigned long long size)
{
    unsigned long long tmp = size & (~size + 1);
    if (tmp == size)
        return size;
    else {
        tmp = 2;
        while (size >>= 1 != 0) {
            tmp += 1;
        }
        return tmp;
    }
}

void *kalloc(unsigned long long size)
{
    unsigned long long log_2_blockSize = round(size)
    if (log_2_blockSize> 0 && log_2_blockSize <= PAGE_SIZE / 2 && free_list_head != NULL)
    {
        if (!used_Blocklist[log_2_blockSize].is_full) {
            used_Blocklist[log_2_blockSize].used_list_head
        }
        else {
            void* addr = kalloc_page();
        }
    }
    return NULL;
}

void kfree(void *ptr)
{

    
    return;
}
