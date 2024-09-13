#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
// #include <kernel/printk.h>

RefCount kalloc_page_cnt;

struct link
{
    void *addr;
    void *next;
};

void kinit() {
    init_rc(&kalloc_page_cnt);
// 最好用static限制作用于再当前文件
    // KERNLINK + end
    static 
    static void *start_addr = end & 0xfffff000 + 0x1000;
    for (int i = start_addr; i < 0x80000000; i+=4096)
    {
        link a;
        link -> addr = i + FFFF0000;
        link -> next
    }
    static 
    // static int page_id;

}

void* kalloc_page() {
    increment_rc(&kalloc_page_cnt);
// 可用内存从end开始，但是end没有按照4k对齐

    return NULL;
}

void kfree_page(void* p) {
    decrement_rc(&kalloc_page_cnt);
// 不能用递归
    return;
}

void* kalloc(unsigned long long size) {
    return NULL;
}

void kfree(void* ptr) {
    return;
}
