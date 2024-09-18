#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
// #include <kernel/printk.h>

RefCount kalloc_page_cnt;

typedef struct Node {
    void *addr;
    struct Node *next;
} Node;

static Node *free_list_head = NULL;

extern char end[];

void kinit()
{
    init_rc(&kalloc_page_cnt);

    // 最好用static限制作用于再当前文件
    // KERNLINK + end

    static void *start_addr =
            (void *)((unsigned long)end & 0xfffff000 + 0x1000);
    unsigned long max_addr = 0x80000000;

    for (int addr = start_addr; addr < max_addr; addr += 4096) {
        Node *page = (Node *)addr;
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

    return NULL;
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);
    // 不能用递归
    return;
}

void *kalloc(unsigned long long size)
{
    return NULL;
}

void kfree(void *ptr)
{
    return;
}
