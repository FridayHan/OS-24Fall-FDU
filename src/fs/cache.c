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

Bitmap(bitmap, MAX_DATA_BLOCKS);

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
    usize running_ops;         // 当前正在运行的原子操作数量
    bool checkpoint_done;      // 检查点是否已完成
    usize log_size;            // 日志区域的总大小（块数）
    usize committed_blocks;    // 已提交但未检查点的块数量
    usize max_running_ops;     // 最大允许运行的原子操作数

    ListNode log_list;         // 日志中记录的块列表（可选：双向链表管理日志记录）
    usize block_no[LOG_MAX_SIZE]; // 当前记录在日志中的块号数组
    usize num_blocks;          // 当前日志中块的数量

    SpinLock log_lock;         // 用于保护日志结构的锁
    // ConditionVariable log_cv; // 用于原子操作等待的条件变量
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
    usize count = 0;
    acquire_spinlock(&lock);
    ListNode *node = head.next;
    while (node != &head) {
        count++;
        node = node->next;
    }
    release_spinlock(&lock);
    return count;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    acquire_spinlock(&lock);
    ListNode *node = head.next;
    Block *block = NULL;

    // 查找指定块号的块
    while (node != &head) {
        block = container_of(node, Block, node);
        if (block->block_no == block_no) {
            if (!block->acquired) {
                block->acquired = true; // 锁定块
                release_spinlock(&lock);
                acquire_sleeplock(&block->lock);
                return block;
            } else {
                block = NULL; // 已被锁定
                break;
            }
        }
        node = node->next;
    }

    return NULL;

    // // 如果没有找到，分配新块
    // block = kalloc(sizeof(Block));
    // init_block(block);
    // block->block_no = block_no;

    // // 从磁盘读取内容
    // device_read(block);

    // // 添加到缓存链表
    // _insert_into_list(&head, &block->node);
    // block->acquired = true;

    // release_spinlock(&lock);
    // acquire_sleeplock(&block->lock);
    // return block;
}

// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    ASSERT(block->acquired);
    acquire_spinlock(&lock);
    block->acquired = false; // 释放块
    release_spinlock(&lock);
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

    init_bitmap(bitmap, sblock->num_data_blocks);

    // 初始化日志头
    memset(&header, 0, sizeof(header));
    read_header(); // 从磁盘加载日志头

    // 恢复日志
    if (header.num_blocks > 0) {
        printk("Recovering from crash: %lld blocks in log\n", header.num_blocks);

        // 依次将日志块写回原始位置
        for (usize i = 0; i < header.num_blocks; i++) {
            Block *block = kalloc(sizeof(Block));
            init_block(block);

            block->block_no = header.block_no[i];
            device_read(block); // 从日志读取块内容
            device_write(block); // 写回块到原始位置

            kfree(block); // 释放内存

            // 将后面的块号向前移动覆盖当前块号
            for (usize j = i + 1; j < header.num_blocks; j++) {
                header.block_no[j - 1] = header.block_no[j];
            }
            header.num_blocks--; // 更新日志头中的块数量
            i--; // 调整索引以继续恢复下一块
        }

        ASSERT(header.num_blocks == 0);

        // 清空日志
        memset(&header, 0, sizeof(header));
        write_header(); // 更新磁盘上的日志头
    }

    printk("Block cache initialized.\n");
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    if (!ctx) PANIC();

    acquire_spinlock(&lock);
    while (log.running_ops >= OP_MAX_NUM_BLOCKS) { // TODO: 为什么是 OP_MAX_NUM_BLOCKS？
        sleep(&log, &lock); // 等待其他操作完成
    }
    log.running_ops++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    ctx->ts = get_timestamp(); // 分配时间戳
    release_spinlock(&lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if (block == NULL) PANIC(); // 块指针不能为空

    acquire_sleeplock(&block->lock);

    // 确保块号在范围内且已分配
    if (!bitmap_get(bitmap, block->block_no)) {
        release_sleeplock(&block->lock);
        PANIC(); // 同步未分配的块
    }

    // 如果有上下文，记录日志
    if (ctx) {
        if (ctx->rm == 0) PANIC(); // 达到块数量上限，无法继续同步
        ctx->rm--;                // 减少可用块数
        acquire_spinlock(&log.log_lock);
        if (log.num_blocks < LOG_MAX_SIZE) {
            log.block_no[log.num_blocks++] = block->block_no; // 记录块号
        }
        release_spinlock(&log.log_lock);
    }

    // 写入块到磁盘
    device_write(block);

    release_sleeplock(&block->lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    if (!ctx) PANIC();

    acquire_spinlock(&lock);
    while (!log.checkpoint_done) {
        sleep(&log, &lock); // 等待检查点完成
    }
    log.running_ops--;
    release_spinlock(&lock);
    wakeup(&log); // 唤醒等待中的线程
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    if (ctx == NULL) PANIC(); // 确保上下文不为空

    acquire_spinlock(&lock);

    // 遍历位图，查找第一个空闲块
    usize block_no = (usize)-1;
    for (usize i = 0; i < sblock->num_data_blocks; i++) {
        if (!bitmap_get(bitmap, i)) { // 位图中对应位为 0，表示空闲
            bitmap_set(bitmap, i);    // 将其标记为已分配
            block_no = i;
            break;
        }
    }

    if (block_no == (usize)-1) {
        release_spinlock(&lock);
        PANIC(); // 无可用块
    }

    // 获取块并初始化
    Block *block = bcache.acquire(block_no);
    memset(block->data, 0, BLOCK_SIZE); // 清空块数据
    bcache.sync(ctx, block);           // 同步块到磁盘
    bcache.release(block);

    release_spinlock(&lock);
    return block_no;
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
    if (ctx == NULL) PANIC(); // 确保上下文不为空

    if (block_no >= sblock->num_data_blocks) {
        return; // 块号无效，直接返回
    }

    acquire_spinlock(&lock);

    // 将块对应的位设置为 0
    bitmap_clear(bitmap, block_no);

    release_spinlock(&lock);
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