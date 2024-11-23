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
    Semaphore log_sem;         // 用于同步的信号量
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
    ListNode *node = head.next;
    while (node != &head) {
        count++;
        node = node->next;
    }
    printk("get_num_cached_blocks: %lld\n", count);
    return count;
}

static void evict_block() {
    Block *to_evict = NULL;
    usize oldest_time = (usize)-1;

    // printk("evict_block acquire_spinlock\n");
    // acquire_spinlock(&lock);

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
        // if (to_evict->pinned) device_write(to_evict); // 同步脏块
        _detach_from_list(&to_evict->node);          // 从链表中移除
        kfree(to_evict);                             // 释放内存
    }
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    // printk("cache_acquire acquire_spinlock\n");
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
                // printk("got: acquiring sleeplock\n");
                acquire_sleeplock(&block->lock);
                return block;
            } else {
                printk("Block %lld is already acquired\n", block_no);
                release_spinlock(&lock);
                // printk("wait: acquiring sleeplock\n");
                acquire_sleeplock(&block->lock);
                ASSERT(!block->acquired);
                ASSERT(block->valid);
                block->acquired = true; // 标记为已获取
                block->last_accessed_time = global_timestamp++; // 更新访问时间
                printk("last_accessed_time: %lld\n", block->last_accessed_time);
                // printk("releasing sleeplock\n");
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
    if (!block) {
        PANIC();
        return;
    }

    // 确保块处于已占用状态
    ASSERT(block->acquired);

    // 加锁，更新块状态
    // printk("cache_release acquire_spinlock\n");
    acquire_spinlock(&lock);
    block->acquired = false; // 标记为未被占用
    release_spinlock(&lock);

    // 释放块上的睡眠锁
    // printk("releasing sleeplock\n");
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

    init_sem(&log.log_sem, 0);

    bitmap_block = kalloc(sizeof(Block));
    init_block(bitmap_block);
    bitmap_block->block_no = sblock->bitmap_start;
    device_read(bitmap_block);

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

    // printk("Block cache initialized.\n");
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    if (!ctx) PANIC();

    // printk("Begin op: acquiring lock\n");
    acquire_spinlock(&lock);

    while (log.running_ops >= OP_MAX_NUM_BLOCKS) {
        release_spinlock(&lock);
        printk("Too many running ops, waiting...\n");
        wait_sem(&log.log_sem); // 等待其他操作完成
        printk("Woke up\n");
        // printk("Begin op: acquiring lock\n");
        acquire_spinlock(&lock);
        // sleep(&log, &lock); // 等待其他操作完成
    }

    log.running_ops++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    // ctx->ts = get_timestamp(); // 分配时间戳
    release_spinlock(&lock);
    printk("Begin op: done\n");
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if (block == NULL) PANIC(); // 块指针不能为空

    // 确保块号在范围内且已分配
    if (!bitmap_get(bitmap_block->data, block->block_no)) {
        printk("Block %lld is not allocated\n", block->block_no);
        
        acquire_spinlock(&lock);
        bitmap_set(bitmap_block->data, block->block_no); // 将块标记为已分配
        release_spinlock(&lock);

        bcache.sync(ctx, bitmap_block); // 同步位图到磁盘
    }

    // 如果有上下文，记录日志
    if (ctx && block->pinned) {
        printk("Syncing block %lld\n", block->block_no);
        if (ctx->rm == 0) PANIC(); // 达到块数量上限，无法继续同步
        ctx->rm--;                // 减少可用块数
        // printk("sync acquiring lock\n");
        acquire_spinlock(&log.log_lock);
        if (log.num_blocks < LOG_MAX_SIZE) {
            log.block_no[log.num_blocks++] = block->block_no; // 记录块号
        } else {
            PANIC(); // 日志已满，无法继续记录
        }
        // Block *log_block = kalloc(sizeof(Block));
        // init_block(log_block);
        // log_block->block_no = sblock->log_start + sblock->num_log_blocks + log.committed_blocks;
        // log_block->valid = true;
        // log_block->pinned = false;
        // memcpy(log_block->data, block->data, BLOCK_SIZE); // 复制块内容
        // _insert_into_list(&log.log_list, &log_block->node); // 将块添加到日志链表
        // log.committed_blocks++; // 增加已提交但未检查点的块数量
        // // printk("sync releasing lock\n");
        // release_spinlock(&log.log_lock);

        // device_write(sblock->log_start + sblock->num_log_blocks + log.committed_blocks - 1); // 将块写入日志
        // block->pinned = false; // 清除脏标记
        release_spinlock(&log.log_lock);
    }

    printk("Writing block %lld\n", block->block_no);
    // 写入块到磁盘
    device_write(block);

    // printk("sync: releasing sleeplock\n");
    // release_sleeplock(&block->lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    if (!ctx) PANIC();

    log.running_ops--; // Decrement the running operation counter
    post_sem(&log.log_sem); // Notify waiting operations

    if (log.running_ops != 0) {
        wait_sem(&log.log_sem); // 等待其他操作完成
        return;
    }

    acquire_spinlock(&log.log_lock);
    printk("Atomic operation started.\n");
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *log_block = kalloc(sizeof(Block));
        init_block(log_block);
        log_block->block_no = sblock->log_start + sblock->num_log_blocks + i;
        log_block->valid = true;
        log_block->pinned = false;

        Block *block = cache_acquire(header.block_no[i]);
        memcpy(log_block->data, block->data, BLOCK_SIZE); // 复制块内容
        device_write(log_block); // 将块写入日志
        cache_release(block);
    }
    printk("Atomic operation ended successfully.\n");

    // TODO : 接下来把log写入磁盘即可
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *block = cache_acquire(header.block_no[i]);
        device_write(block);
        cache_release(block);
    }

    // Clear log metadata
    header.num_blocks = 0;
    release_spinlock(&log.log_lock);




    printk("Atomic operation ended successfully.\n");

    // for (usize i = 0; i < log.committed_blocks; i++) {
    //     usize log_block_no = sblock->log_start + i;

    //     Block *log_block = kalloc(sizeof(Block));
    //     init_block(log_block);
    //     log_block->block_no = log_block_no;
    //     memset(log_block->data, 0, BLOCK_SIZE); // 填充空数据
    //     device_write(log_block); // 将空数据写入日志块
    //     kfree(log_block); // 释放临时块内存
    // }

    // // Clear log metadata
    // log.committed_blocks = 0;
    // log.num_blocks = 0;





    // Block *log_block = kalloc(sizeof(Block));
    // init_block(log_block);
    // log_block->block_no = sblock->log_start + sblock->num_log_blocks + log.committed_blocks;
    // log_block->valid = true;
    // log_block->pinned = false;
    // memcpy(log_block->data, block->data, BLOCK_SIZE); // 复制块内容
    // _insert_into_list(&log.log_list, &log_block->node); // 将块添加到日志链表
    // log.committed_blocks++; // 增加已提交但未检查点的块数量
    // // printk("sync releasing lock\n");
    // release_spinlock(&log.log_lock);

    // device_write(sblock->log_start + sblock->num_log_blocks + log.committed_blocks - 1); // 将块写入日志
    // block->pinned = false; // 清除脏标记




    // release_spinlock(&log.log_lock);

    printk("Atomic operation ended successfully.\n");
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    if (ctx == NULL) PANIC(); // 确保上下文不为空

    printk("Allocating block\n");
    acquire_spinlock(&lock);

    // 遍历位图，查找第一个空闲块
    usize block_no = (usize)-1;
    for (usize i = 0; i < sblock->num_data_blocks; i++) {
        if (!bitmap_get(bitmap_block->data, i)) { // 位图中对应位为 0，表示空闲
            bitmap_set(bitmap_block->data, i);    // 将其标记为已分配
            block_no = i;
            break;
        }
    }

    if (block_no == (usize)-1) {
        release_spinlock(&lock);
        PANIC(); // 无可用块
    }

    printk("Allocated block %lld\n", block_no);

    // 获取块并初始化
    Block *block = kalloc(sizeof(Block));
    memset(block->data, 0, BLOCK_SIZE); // 清空块数据
    init_block(block);
    block->block_no = block_no;
    block->valid = true; // 标记为有效
    block->pinned = false; // 标记为未固定
    _insert_into_list(&head, &block->node); // 将块添加到缓存链表

    bcache.sync(ctx, bitmap_block); // 同步位图到磁盘


    printk("Synced block %lld\n", block_no);
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

    printk("Freeing block %lld\n", block_no);
    acquire_spinlock(&lock);

    // 将块对应的位设置为 0
    bitmap_clear(bitmap_block->data, block_no);
    bcache.sync(ctx, bitmap_block); // 同步位图到磁盘

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