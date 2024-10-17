#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

#define Slab_division 4

RefCount kalloc_page_cnt;

void *nowAvailable;
int free_couter = 0;
SpinLock lock;

int upper_log2(unsigned long long n)
{
    int result = 0;

    while (n > 0) {
        n >>= 1;
        result++;
    }
    return result;
}

void *nowAvailable2;
int free_counter2 = 0;
SpinLock lock2;
int PAGE_SIZE2 = 4096 * 2 * 2 * 2;
int division = 2048;

static inline void *Slab_start2(void *x)
{
    int k = upper_log2(PAGE_SIZE2) - 1;
    return ((void *)(((u64)x >> k) << k));
}

void *Slab_config[10 * Slab_division];
// stores the start address of available address
SpinLock locks[10 * Slab_division];
// when accessing the slab config, ask for its corresponding lock

void kinit()
{
    init_rc(&kalloc_page_cnt);
    extern char end[];
    int page_number = (int)(((char *)PHYSTOP - end) / PAGE_SIZE);
    nowAvailable =
            (void *)(P2K((void *)((char *)PHYSTOP - page_number * PAGE_SIZE)));
    nowAvailable2 = (void *)(P2K((void *)((char *)PHYSTOP - PAGE_SIZE2)));

    init_spinlock(&lock);
    init_spinlock(&lock2);

    for (int i = 0; i < 10; i++) {
        init_spinlock(&locks[i]);
        Slab_config[i] = NULL;
    }
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);

    acquire_spinlock(&lock);
    void *result = nowAvailable;
    if (!free_couter) {
        nowAvailable = (void *)((char *)nowAvailable + PAGE_SIZE);
    } else {
        nowAvailable = (void *)(*(u64 *)nowAvailable);
        free_couter--;
    }
    release_spinlock(&lock);
    // printk("page1 %p is allocated\n", result);

    return result;
}

void *kalloc_page2()
{
    int k = PAGE_SIZE2 / PAGE_SIZE;
    for (int i = 0; i < k; i++) {
        increment_rc(&kalloc_page_cnt);
    }

    acquire_spinlock(&lock2);
    void *result = nowAvailable2;
    if (!free_counter2) {
        nowAvailable2 = (void *)((char *)nowAvailable2 - PAGE_SIZE2);
    } else {
        nowAvailable2 = (void *)(*(u64 *)nowAvailable2);
        free_counter2--;
    }
    release_spinlock(&lock2);
    // printk("page2 %p is allocated\n", result);

    return result;
}

void kfree_page(void *p)
{
    decrement_rc(&kalloc_page_cnt);

    acquire_spinlock(&lock);
    free_couter++;

    void *next = nowAvailable;
    nowAvailable = p;
    *(u64 *)nowAvailable = (u64)next;
    release_spinlock(&lock);
    // printk("page1 %p is allocated\n", p);

    return;
}

void kfree_page2(void *p)
{
    int k = PAGE_SIZE2 / PAGE_SIZE;
    for (int i = 0; i < k; i++) {
        decrement_rc(&kalloc_page_cnt);
    }

    acquire_spinlock(&lock2);
    free_counter2++;

    void *next = nowAvailable2;
    nowAvailable2 = p;
    *(u64 *)nowAvailable2 = (u64)next;
    release_spinlock(&lock2);
    // printk("page2 %p is allocated\n", p);

    return;
}

int exp2(int x)
{
    int result = 1;
    while (x > 0) {
        x--;
        result <<= 1;
    }
    return result;
}

static inline void *get_header(void *start)
{
    return ((void *)(*(u64 *)start));
}

void init_slab(void *start, int unit_size, short id)
{
    *(u64 *)start = ((u64)((char *)start + 32) | 1); // init nowAvailable
    int *unit = (int *)((char *)start + 8);
    *unit = unit_size; // unit_size setting
    short *a = (short *)((char *)start + 12);
    *a = 0;
    *(a + 1) = id;
    u64 *next = (u64 *)((char *)start + 16);
    *next = 0; // next slab setting
    *(next + 1) = 0;
}

static inline void *Slab_start(void *x)
{
    return ((void *)(((u64)x >> 12) << 12));
}

static inline void *Get_Available(void *start)
{
    return (void *)(((u64)get_header(start)) & ~1ULL);
}

static inline int whether_available(void *start)
{
    return (int)(((u64)get_header(start)) & 1);
}
static inline void set_whether_available(void *start)
{
    *(u64 *)start |= 1;
}
static inline void clear_whether_available(void *start)
{
    *(u64 *)start &= ~1ULL;
}

static inline int Get_UnitSize(void *start)
{
    return (*(int *)((char *)start + 8));
}

static inline short *Get_FreeCounter(void *start)
{
    return ((short *)((char *)start + 12));
}

static inline void set_Available(void *start, void *next)
{
    int a = whether_available(start);
    *(u64 *)start = ((u64)next | a);
}

static inline void *Get_tail(void *start)
{
    return (void *)(*(u64 *)((char *)start + 16));
}

static inline int Get_remain(void *start)
{
    return (int)(((u64)Get_tail(start)) & 0xFFF);
}

static inline void *Get_NextSlab(void *start)
{
    return (void *)(((u64)Get_tail(start)) & ~0xFFF);
}

static inline void *Get_PrevSlab(void *start)
{
    return (void *)(*(u64 *)((char *)start + 24));
}

static inline void Add_remain(void *start)
{
    int new = Get_remain(start);
    new ++;
    void *next = Get_NextSlab(start);
    *(u64 *)((char *)start + 16) = ((u64)next | new);
}

static inline void Minus_remain(void *start)
{
    int new = Get_remain(start);
    new --;
    void *next = Get_NextSlab(start);
    *(u64 *)((char *)start + 16) = ((u64)next | new);
}

static inline void set_slab_next(void *start, void *next)
{
    int remain = Get_remain(start);
    *(u64 *)((char *)start + 16) = ((u64)next | remain);
}

static inline void set_slab_prev(void *start, void *next)
{
    *(u64 *)((char *)start + 24) = (u64)next;
}

static inline void *start_BGE_512(void *ptr)
{
    return (void *)(*((u64 *)ptr - 1));
}

static inline int get_id(void *start)
{
    return (int)(*(short *)((char *)start + 14));
}

void *find_slab_element(void *start)
{
    void *result = Get_Available(start);
    short *slab_free_counter = Get_FreeCounter(start);
    int unit_size = Get_UnitSize(start);

    Add_remain(start);
    int slab_id = get_id(start);

    if (!(*slab_free_counter)) {
        void *next = (void *)((char *)result + unit_size);

        if (Slab_start((void *)((char *)next + unit_size - 1)) !=
            Slab_start(result)) {
            next = NULL;
            clear_whether_available(start);
            Slab_config[slab_id] = Get_NextSlab(start);
            if (Slab_config[slab_id])
                set_slab_prev(Slab_config[slab_id], NULL);
        }
        set_Available(start, next);
    } else {
        (*slab_free_counter)--;
        void *next = (void *)(*(u64 *)result);
        if (next == NULL) {
            clear_whether_available(start);
            Slab_config[slab_id] = Get_NextSlab(start);
            if (Slab_config[slab_id])
                set_slab_prev(Slab_config[slab_id], NULL);
        }
        set_Available(start, next);
    }
    return result;
}

void *find_slab_element2(void *start)
{
    void *result = Get_Available(start);
    short *slab_free_counter = Get_FreeCounter(start);
    int unit_size = Get_UnitSize(start);

    Add_remain(start);
    int slab_id = get_id(start);

    if (!(*slab_free_counter)) {
        void *next = (void *)((char *)result + unit_size);

        if (Slab_start2((void *)((char *)next + unit_size - 1)) !=
            Slab_start2(result)) {
            // printk("full!\n");
            next = NULL;
            clear_whether_available(start);
            Slab_config[slab_id] = Get_NextSlab(start);
            if (Slab_config[slab_id])
                set_slab_prev(Slab_config[slab_id], NULL);
        }
        set_Available(start, next);
    } else {
        (*slab_free_counter)--;
        void *next = (void *)(*(u64 *)result);
        if (next == NULL) {
            clear_whether_available(start);
            Slab_config[slab_id] = Get_NextSlab(start);
            if (Slab_config[slab_id])
                set_slab_prev(Slab_config[slab_id], NULL);
        }
        set_Available(start, next);
    }
    return result;
}

void *kalloc(unsigned long long size)
{
    if (size <= 0)
        return NULL;

    int target_size = upper_log2(size);
    int target_slab_id = target_size > 3 ? target_size - 3 : 0;
    target_size = target_size > 3 ? exp2(target_size) : 8;

    int bound = Slab_division << 3;
    if (target_size > bound) {
        int s = target_size / 2;
        int unit = s / Slab_division;
        int k = upper_log2(bound) - 4;
        for (int i = 0; i < Slab_division; i++) {
            if (size < (u64)(s + (i + 1) * unit)) {
                target_slab_id =
                        (target_slab_id - k - 1) * Slab_division + k + 1 + i;
                target_size = s + unit * (i + 1);
                break;
            }
        }
    }

    acquire_spinlock(&locks[target_slab_id]);
    void *target_slab = Slab_config[target_slab_id];
    void *result;
    if (target_slab == NULL) {
        void *newPage = target_size < division ? kalloc_page() : kalloc_page2();
        init_slab(newPage, target_size, target_slab_id);
        Slab_config[target_slab_id] = newPage;
        target_slab = newPage;
    }

    result = target_size < division ? find_slab_element(target_slab) :
                                      find_slab_element2(target_slab);

    if (result == NULL) {
        printk("unit_size is %d\n", target_size);
    }
    // printk("success alloc %lld bytes at %p\n", size, result);
    release_spinlock(&locks[target_slab_id]);
    return result;
}

void kfree(void *ptr)
{
    void *start = ptr < (void *)(P2K(0x60000000)) ? Slab_start(ptr) :
                                                    Slab_start2(ptr);
    int unit_size = Get_UnitSize(start);
    int slab_id = get_id(start);

    acquire_spinlock(&locks[slab_id]);

    Minus_remain(start);
    if (Get_remain(start) == 0) {
        void *prev = Get_PrevSlab(start);
        void *next = Get_NextSlab(start);
        if (next)
            set_slab_prev(next, prev);
        if (prev)
            set_slab_next(prev, next);
        if (Slab_config[slab_id] == start) {
            Slab_config[slab_id] = next;
        }
        unit_size < division ? kfree_page(start) : kfree_page2(start);
        release_spinlock(&locks[slab_id]);
        return;
    }

    short *freeCounter = Get_FreeCounter(start);

    (*freeCounter)++;
    void *nextelement = Get_Available(start);
    *(u64 *)ptr = (u64)nextelement;
    set_Available(start, ptr); // change the content in the slab

    if (!whether_available(start)) {
        void *nextslab = Slab_config[slab_id];
        Slab_config[slab_id] = start;
        set_slab_next(start, nextslab); // change the available slab to this
        set_slab_prev(start, NULL);
        if (nextslab)
            set_slab_prev(nextslab, start);
        set_whether_available(start);
    }

    // printk("free %d bytes at %p\n", unit_size, ptr);
    release_spinlock(&locks[slab_id]);

    return;
}
