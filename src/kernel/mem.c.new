#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

RefCount kalloc_page_cnt;
SpinLock mem_lock;
void* freePPhead=0;
void* usrMemStart=0;
#define rd2p(x) (((usize)(x)+4095)&~4095)
#define ALLOC_NUM 26
u32 SLUBDIR[ALLOC_NUM] = {8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 128, 144, 176, 208, 256, 304, 368, 440, 512, 808, 1024, 2032, 4064};

struct pageHead
{
    void * prev;
    void * next;
    void * listHead;
    u32 blockSize;
    u32 bkCnt;
};

struct allocator
{
    void *avlHead;
    void *fullHead;
};

struct memCache
{
    struct allocator slubs[ALLOC_NUM];
}cpuCache[4];

void memCache_init()
{
    for(int i=0; i<4; i++)
    {
        for(int j=0; j<ALLOC_NUM; j++)
        {
            cpuCache[i].slubs[j].avlHead = 0;
            cpuCache[i].slubs[j].fullHead = 0;
        }
    }
}

u32 round_up_slub(usize x){
   int i=0;
   while(i<ALLOC_NUM && x>SLUBDIR[i]) i++;
   if(i == ALLOC_NUM) i--;
   return SLUBDIR[i];
}

u32 get_index(u32 sz)
{
    u32 i=0;
    while(i<ALLOC_NUM && SLUBDIR[i] != sz) i++;
    if(i == ALLOC_NUM) i--;
    return i;
}

void kinit() {
    init_rc(&kalloc_page_cnt);
    extern char end[];
    freePPhead = usrMemStart = (void *)(rd2p(end));
    usize phyStop = P2K(PHYSTOP);
    for(usize i = (u64)(usrMemStart); i<phyStop; i+=PAGE_SIZE)
    {
        void **curPP = (void **)(i);
        *curPP = freePPhead;
        freePPhead = (void *)i;
    }
    init_spinlock(&mem_lock);
    memCache_init();
    return;
}

void* kalloc_page() {
    acquire_spinlock(&mem_lock);
    increment_rc(&kalloc_page_cnt);
    void *nextPP = *((void **)freePPhead);
    void *allocPage = freePPhead;
    freePPhead = nextPP;
    release_spinlock(&mem_lock);
    return allocPage;
}

void kfree_page(void* p) {
    acquire_spinlock(&mem_lock);
    decrement_rc(&kalloc_page_cnt);
    p = (void *)PAGE_BASE(p);
    void **insertPP = p;
    *insertPP = freePPhead;
    freePPhead = p;
    release_spinlock(&mem_lock);
    return;
}

void* kalloc(unsigned long long size) {
    void *allocBlock = 0;
    u32 rdSize = round_up_slub(size);
    u32 slubIndex = get_index(rdSize);
    usize id = cpuid();
    struct allocator* curSlub = &cpuCache[id].slubs[slubIndex];
    
    if(!curSlub->avlHead)
    {
        struct pageHead* newPage = (struct pageHead *)kalloc_page();
        newPage->next = newPage->prev = (void *)newPage;
        newPage->blockSize = rdSize;
        newPage->listHead = (void *)newPage + sizeof(struct pageHead);
        newPage->bkCnt = 0;

        void* pageEnd = (void *)newPage + PAGE_SIZE;
        for(void *ptr = newPage->listHead; ptr + rdSize <= pageEnd; ptr += rdSize)
        {
            void **curBlock = (void **)ptr;
            *curBlock = newPage->listHead;
            newPage->listHead = ptr;
            newPage->bkCnt++;
        }
        curSlub->avlHead = (void *)newPage;
    }
    
    struct pageHead* avlPage = (struct pageHead *)curSlub->avlHead;
    allocBlock = avlPage->listHead;
    void **next = (void **)allocBlock;
    if(avlPage->listHead == *next)
    {
        // move to full list, 将listhead设为0表示已满
        struct pageHead* movinHead = (struct pageHead*)curSlub->avlHead;
        // ops, only one free page left
        if(movinHead->prev == movinHead->next)
        {
            curSlub->avlHead = 0;
        }
        else
        {
            struct pageHead* nextHead = (struct pageHead*)movinHead->next;
            nextHead->prev = movinHead->next;
            curSlub->avlHead = movinHead->next;
        }

        if(!curSlub->fullHead)
        {
            movinHead->prev = movinHead->next = (void*)movinHead;
        }
        else
        {
            struct pageHead* oldHead = (struct pageHead*)curSlub->fullHead;
            movinHead->next = (void *)oldHead;
            movinHead->prev = (void*)movinHead;
            oldHead->prev = (void *)movinHead;
        }
        curSlub->fullHead = (void*)movinHead;
        movinHead->listHead = 0;
    }
    else avlPage->listHead = *next;
    avlPage->bkCnt--;
    return allocBlock;
}

void kfree(void* ptr) {
    struct pageHead* rlsHead = (struct pageHead*)PAGE_BASE(ptr);
    // full list
    if(!rlsHead->listHead)
    {
        rlsHead->listHead = ptr;
        void **next = (void **)ptr;
        *next = ptr;

        //move to available list
        u64 id = cpuid();
        //u64 slubIndex = __builtin_ctzll(rlsHead->blockSize)-3;
        u32 slubIndex = get_index(rlsHead->blockSize);
        struct allocator* curSlub = &cpuCache[id].slubs[slubIndex];
        struct pageHead* nextHead = (struct pageHead*)rlsHead->next;
        struct pageHead* prevHead = (struct pageHead*)rlsHead->prev;
        // case 0. ops, only one full page left
        if(rlsHead->prev == rlsHead->next)
        {
            curSlub->fullHead = 0;
        }
        // case 1. end of full list
        else if(rlsHead->next == (void*)rlsHead)
        {
            prevHead->next = (void *)prevHead;
        }
        // case 2. head of full list
        else if(rlsHead->prev == (void *)rlsHead)
        {
            nextHead->prev = (void *)nextHead;
            curSlub->fullHead = (void *)nextHead;
        }
        // case 3. normal
        else
        {
            nextHead->prev = rlsHead->prev;
            prevHead->next = rlsHead->next;
        }

        if(!curSlub->avlHead)
        {
            rlsHead->prev = rlsHead->next = (void *)rlsHead;
        }
        else
        {
            struct pageHead* oldHead = (struct pageHead*)curSlub->avlHead;
            rlsHead->next = (void *)oldHead;
            rlsHead->prev = (void *)rlsHead;
            oldHead->prev = (void *)rlsHead;
        }
        curSlub->avlHead = (void *)rlsHead;
    }
    else        // available list
    {
        void **next = (void **)ptr;
        *next = rlsHead->listHead;
        rlsHead->listHead = ptr;
    }
    rlsHead->bkCnt ++;
    if(rlsHead->bkCnt == 4064/rlsHead->blockSize)
    {
        u64 id = cpuid();
        u32 slubIndex = get_index(rlsHead->blockSize);
        struct allocator* curSlub = &cpuCache[id].slubs[slubIndex];
        struct pageHead* nextHead = (struct pageHead*)rlsHead->next;
        struct pageHead* prevHead = (struct pageHead*)rlsHead->prev;
        // case 0. ops, only one avl page left
        if(rlsHead->prev == rlsHead->next)
        {
            curSlub->avlHead = 0;
        }
        // case 1. end of avl list
        else if(rlsHead->next == (void*)rlsHead)
        {
            prevHead->next = (void *)prevHead;
        }
        // case 2. head of avl list
        else if(rlsHead->prev == (void *)rlsHead)
        {
            nextHead->prev = (void *)nextHead;
            curSlub->avlHead = (void *)nextHead;
        }
        // case 3. normal
        else
        {
            nextHead->prev = rlsHead->prev;
            prevHead->next = rlsHead->next;
        }
        kfree_page((void*)rlsHead);
    }
    return;
}
