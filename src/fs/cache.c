#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <common/spinlock.h>


/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device;

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

static LogHeader header; // in-memory copy of log header block.

static Block *bitmap_block; // the block containing bitmap.

static usize cache_size; // the size of the cache.

/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* your fields here */
    u32 id;                    // 当前原子操作的ID
    bool checkpoint_done;      // 检查点是否已完成
    u32 running_ops;       // 当前正在运行的原子操作数量

    SpinLock log_lock;         // 用于保护日志结构的锁
    Semaphore op_num_sem;         // 用于同步的信号量
} log;

// read the content from disk.
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    // usize count = 0;
    // ListNode *node = head.next;
    // while (node != &head) {
    //     count++;
    //     node = node->next;
    // }
    // printk("get_num_cached_blocks: %lld\n", count);
    // return count;

    return cache_size;
}

static void evict_block() {
    Block *to_evict = NULL;
    usize oldest_time = (usize)-1;

    ListNode *node = head.next;
    while (node != &head) {
        Block *block = container_of(node, Block, node);

        // 跳过无法驱逐的块
        if (block->pinned || block->acquired) {
            node = node->next;
            continue;
        }

        // 找到访问时间最早的块
        if (block->last_accessed_time < oldest_time) {
            oldest_time = block->last_accessed_time;
            to_evict = block;
        }
        node = node->next;
    }

    // 如果找到块，驱逐它
    if (to_evict) {
        _detach_from_list(&to_evict->node);          // 从链表中移除
        cache_size--;
    }
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    acquire_spinlock(&lock);

    ListNode *node = head.next;
    Block *block = NULL;

    // 查找缓存中是否存在该块
    while (node != &head) {
        block = container_of(node, Block, node);
        if (block->block_no == block_no) {
            printk("Block %lld found in cache\n", block_no);
            ASSERT(block->valid);
            if (!block->acquired) {
                ASSERT(block->valid);
                block->acquired = true; // 标记为已获取
                block->last_accessed_time = global_timestamp++; // 更新访问时间
                printk("last_accessed_time: %lld\n", block->last_accessed_time);
                release_spinlock(&lock);
                acquire_sleeplock(&block->lock);
                return block;
            } else {
                printk("Block %lld is already acquired\n", block_no);
                release_spinlock(&lock);
                acquire_sleeplock(&block->lock);
                ASSERT(!block->acquired);
                ASSERT(block->valid);
                block->acquired = true; // 标记为已获取
                block->last_accessed_time = global_timestamp++; // 更新访问时间
                printk("last_accessed_time: %lld\n", block->last_accessed_time);
                release_sleeplock(&block->lock);
                return block;
            }
        }
        node = node->next;
    }

    // 判断缓存是否还有空间
    if (get_num_cached_blocks() >= EVICTION_THRESHOLD) {
        // 缓存已满，需要释放一些块
        evict_block();
    }

    // 从磁盘读取块内容
    block = kalloc(sizeof(Block));
    init_block(block);

    block->block_no = block_no;
    device_read(block); // 从磁盘读取块内容
    block->valid = true; // 标记为有效

    // 将块添加到缓存链表
    _insert_into_list(&head, &block->node);
    cache_size++;
    block->acquired = true; // 标记为已获取
    block->last_accessed_time = global_timestamp++; // 更新访问时间
    printk("last_accessed_time: %lld\n", block->last_accessed_time);
    release_spinlock(&lock);

    // printk("acquiring sleeplock\n");
    acquire_sleeplock(&block->lock);
    return block;
}

// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    // 确保传入的块不为空
    if (!block) { PANIC(); }
    ASSERT(block->acquired);

    // 加锁，更新块状态
    acquire_spinlock(&lock);
    block->acquired = false; // 标记为未被占用
    release_spinlock(&lock);

    // 释放块上的睡眠锁
    release_sleeplock(&block->lock);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    // TODO
    // 初始化全局锁
    init_spinlock(&lock);

    // 初始化缓存链表头
    init_list_node(&head);

    // 初始化日志头
    memset(&header, 0, sizeof(header));
    read_header(); // 从磁盘加载日志头

    init_sem(&log.op_num_sem, MAX_NUM_OP); // 初始化信号量

    log.running_ops = 0; // 初始化运行中的操作数量

    bitmap_block = kalloc(sizeof(Block));
    init_block(bitmap_block);
    bitmap_block->block_no = sblock->bitmap_start;
    device_read(bitmap_block);

    // 从磁盘读取日志，如果非空，那么需要恢复数据

    read_header();
    Block tmp;
    init_block(&tmp);

    for (usize i = 0; i < header.num_blocks; i++) {
        tmp.block_no = sblock->log_start + 1 + i;
        device_read(&tmp);
        tmp.block_no = header.block_no[i];
        device_write(&tmp);
    }

    header.num_blocks = 0;
    write_header();


    // // 恢复日志
    // if (header.num_blocks > 0) {
    //     printk("Recovering from crash: %lld blocks in log\n", header.num_blocks);

    //     // 依次将日志块写回原始位置
    //     for (usize i = 0; i < header.num_blocks; i++) {
    //         Block *block = kalloc(sizeof(Block));
    //         init_block(block);

    //         block->block_no = header.block_no[i];
    //         device_read(block); // 从日志读取块内容
    //         device_write(block); // 写回块到原始位置

    //         kfree(block); // 释放内存

    //         // 将后面的块号向前移动覆盖当前块号
    //         for (usize j = i + 1; j < header.num_blocks; j++) {
    //             header.block_no[j - 1] = header.block_no[j];
    //         }
    //         header.num_blocks--; // 更新日志头中的块数量
    //         i--; // 调整索引以继续恢复下一块
    //     }

    //     ASSERT(header.num_blocks == 0);

    //     // 清空日志
    //     memset(&header, 0, sizeof(header));
    //     write_header(); // 更新磁盘上的日志头
    // }
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    if (!ctx) PANIC();

    wait_sem(&log.op_num_sem); // 判断操作数量是否超过上限

    acquire_spinlock(&lock);
    // 分配一个id
    // ctx->id = oid++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    release_spinlock(&lock);

    acquire_spinlock(&log.log_lock);
    log.running_ops++;
    release_spinlock(&log.log_lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if (!block) PANIC();
    if (!ctx) { device_write(block); return; }

    acquire_spinlock(&log.log_lock);
    for (usize i = 0; i < header.num_blocks; i++) {
        if (header.block_no[i] == block->block_no) {
            release_spinlock(&log.log_lock);
            return;
        }
    }

    if (ctx->rm == 0) PANIC();

    header.num_blocks++;
    header.block_no[header.num_blocks - 1] = block->block_no;
    block->pinned = true;
    ctx->rm--;
    release_spinlock(&log.log_lock);

    // if (!block) PANIC();
    // if (!ctx) { device_write(block); return; }

    // if (!ctx->rm) PANIC(); // 达到块数量上限，无法继续同步
    // printk("Syncing block %lld\n", block->block_no);

    // // 从位图中检查块是否已分配
    // if (!bitmap_get(bitmap_block->data, block->block_no)) {
    //     printk("Block %lld is not allocated\n", block->block_no);
        
    //     acquire_spinlock(&lock);
    //     bitmap_set(bitmap_block->data, block->block_no); // 将块标记为已分配
    //     release_spinlock(&lock);
    //     device_write(bitmap_block); // 将位图写回磁盘
    // }

    // // 如果块被固定，那么需要将其写入日志
    // if (block->pinned) {
    //     printk("Syncing block %lld\n", block->block_no);
    //     ctx->rm--;                // 减少可用块数

    //     acquire_spinlock(&log.log_lock);
    //     if (header.num_blocks < LOG_MAX_SIZE) {
    //         header.block_no[header.num_blocks++] = block->block_no; // 记录块号
    //         write_header(); // 更新日志头
    //     } else {
    //         PANIC(); // 日志已满，无法继续记录
    //     }
    //     release_spinlock(&log.log_lock);
    // }

    // printk("Writing block %lld\n", block->block_no);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    printk("End op\n");
    if (!ctx) PANIC();
    
    acquire_spinlock(&log.log_lock);
    log.running_ops--; // 减少运行中的操作数量
    ctx->rm = 0; // 置零剩余块数

    printk("log.running_ops: %lld\n", log.running_ops);
    if (log.running_ops != 0) {
        post_sem(&log.op_num_sem);
        release_spinlock(&log.log_lock);
        return;
    }

    acquire_spinlock(&log.log_lock);
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *block = cache_acquire(header.block_no[i]);
        usize tmp = block->block_no;
        block->block_no = sblock->log_start + 1 + i;
        device_write(block);
        block->block_no = tmp;
        cache_release(block);
    }

    // 把日志块写回data区域
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *block = cache_acquire(header.block_no[i]);
        device_write(block);
        block->pinned = false;
        cache_release(block);
    }

    // 清空日志头
    header.num_blocks = 0;
    write_header();

    release_spinlock(&log.log_lock);

    post_sem(&log.op_num_sem); // Notify waiting operations
    printk("Atomic operation ended successfully.\n");
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    if (ctx->rm <= 0)
        PANIC();

    usize num_bitmap_blocks = (sblock->num_data_blocks + BIT_PER_BLOCK - 1) / BIT_PER_BLOCK;

    for (usize i = 0; i < num_bitmap_blocks; i++) {
        Block *bitmap_block = cache_acquire(sblock->bitmap_start + i);
        for (usize j = 0; j < BLOCK_SIZE * 8; j++) {
            usize block_no = i * BLOCK_SIZE * 8 + j;
            if (block_no >= sblock->num_blocks) {
                cache_release(bitmap_block);
                PANIC();
            }
            if (!bitmap_get((BitmapCell *)bitmap_block->data, j)) {
                Block *b = cache_acquire(block_no);
                memset(b->data, 0, BLOCK_SIZE);
                cache_sync(ctx, b);
                bitmap_set((BitmapCell *)bitmap_block->data, j);
                cache_sync(ctx, bitmap_block);
                cache_release(b);
                cache_release(bitmap_block);
                return block_no;
            }
        }
        cache_release(bitmap_block);
    }
    return -1;


    // if (ctx == NULL) PANIC(); // 确保上下文不为空

    // printk("Allocating block\n");
    // acquire_spinlock(&lock);

    // // 遍历位图，查找第一个空闲块
    // usize block_no = (usize)-1;
    // for (usize i = 0; i < sblock->num_data_blocks; i++) {
    //     if (!bitmap_get(bitmap_block->data, i)) { // 位图中对应位为 0，表示空闲
    //         bitmap_set(bitmap_block->data, i);    // 将其标记为已分配
    //         block_no = i;
    //         break;
    //     }
    // }

    // if (block_no == (usize)-1) {
    //     release_spinlock(&lock);
    //     PANIC(); // 无可用块
    // }

    // printk("Allocated block %lld\n", block_no);

    // // 获取块并初始化
    // Block *block = kalloc(sizeof(Block));
    // memset(block->data, 0, BLOCK_SIZE); // 清空块数据
    // init_block(block);
    // block->block_no = block_no;
    // block->valid = true; // 标记为有效
    // block->pinned = false; // 标记为未固定
    // _insert_into_list(&head, &block->node); // 将块添加到缓存链表

    // device_write(bitmap_block); // 同步位图到磁盘


    // printk("Synced block %lld\n", block_no);
    // release_spinlock(&lock);
    // return block_no;
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
    if (ctx == NULL) PANIC(); // 确保上下文不为空

    if (block_no >= sblock->num_data_blocks) {
        return; // 块号无效，直接返回
    }

    printk("Freeing block %lld\n", block_no);

    Block *bitmap_block = cache_acquire(block_no / (BLOCK_SIZE * 8) + sblock->bitmap_start);
    bitmap_clear((BitmapCell *)bitmap_block->data, block_no % (BLOCK_SIZE * 8));
    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);

    // acquire_spinlock(&lock);

    // // 将块对应的位设置为 0
    // bitmap_clear(bitmap_block->data, block_no);
    // device_write(bitmap_block); // 同步位图到磁盘

    // release_spinlock(&lock);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};