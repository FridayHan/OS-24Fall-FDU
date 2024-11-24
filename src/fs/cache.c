#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

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

static usize cache_size = 0;

static LogHeader header; // in-memory copy of log header block.

usize global_timestamp = 0;

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
    SpinLock log_lock;
    Semaphore op_num_sem;
    Semaphore check_sem;
    u64 num_ops;
    u64 blocks_occupied;
} log;

// read the content from disk.
static INLINE void device_read(Block *block)
{
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
    return cache_size;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    acquire_spinlock(&lock);
    Block *block;
    bool in_cache = false;

    _for_in_list(p, &head)
    {
        if (p == &head) {
            continue;
        }
        block = container_of(p, Block, node);
        if (block->block_no == block_no) {
            in_cache = true;
            break;
        }
    }

    if (in_cache) {
        while (block->acquired) {
            release_spinlock(&lock);
            wait_sem(&block->lock);
            acquire_spinlock(&lock);
            if (get_sem(&block->lock)) {
                break;
            }
        }
        if (!block->acquired) {
            get_sem(&block->lock);
        }
        block->acquired = true;
        block->last_accessed_time = global_timestamp++;
        release_spinlock(&lock);
        return block;
    }

    if (get_num_cached_blocks() >= EVICTION_THRESHOLD) {
        // 缓存达到限额，需要释放一些块
        evict_block();
    }

    block = (Block *)kalloc(sizeof(Block));
    init_block(block);
    get_sem(&block->lock);
    block->acquired = true;
    block->block_no = block_no;
    block->last_accessed_time = global_timestamp++;

    _insert_into_list(&head, &block->node);
    cache_size++;

    device_read(block);
    block->valid = true;
    release_spinlock(&lock);
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
    block->acquired = false;
    post_sem(&block->lock);
    release_spinlock(&lock);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_list_node(&head);

    init_spinlock(&log.log_lock);
    init_sem(&log.check_sem, 0);
    init_sem(&log.op_num_sem, MAX_NUM_OP);
    log.blocks_occupied = 0;

    // recover log
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
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx)
{
    // TODO
    acquire_spinlock(&log.log_lock);

    if (log.num_ops >= MAX_NUM_OP) {
        _lock_sem(&(log.op_num_sem));
        release_spinlock(&log.log_lock);
        if (!_wait_sem(&(log.op_num_sem), false)) {
            PANIC();
        };
        acquire_spinlock(&log.log_lock);
    }
    log.num_ops++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    release_spinlock(&log.log_lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
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
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&log.log_lock);
    log.num_ops--;
    log.blocks_occupied -= OP_MAX_NUM_BLOCKS;
    ctx->rm = 0;

    if (log.num_ops > 0) {
        _lock_sem(&(log.check_sem));
        post_sem(&log.op_num_sem);
        release_spinlock(&log.log_lock);
        if (!_wait_sem(&(log.check_sem), false)) {
            PANIC();
        };
        return;
    }

    if (log.num_ops == 0) {
        for (usize i = 0; i < header.num_blocks; i++) {
            Block *block = cache_acquire(header.block_no[i]);
            device->write(sblock->log_start + i + 1, block->data);
            block->pinned = false;
            cache_release(block);
        }
        write_header();
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
        post_all_sem(&log.check_sem);
        post_sem(&log.op_num_sem);
    }
    release_spinlock(&log.log_lock);
}

// see `cache.h`.
usize cache_alloc(OpContext *ctx)
{
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
                Block *block = cache_acquire(block_no);
                memset(block->data, 0, BLOCK_SIZE);
                cache_sync(ctx, block);
                bitmap_set((BitmapCell *)bitmap_block->data, j);
                cache_sync(ctx, bitmap_block);
                cache_release(block);
                cache_release(bitmap_block);
                return block_no;
            }
        }
        cache_release(bitmap_block);
    }
    return -1;
}

// see `cache.h`.
void cache_free(OpContext *ctx, usize block_no)
{
    // TODO
    Block *bitmap_block = cache_acquire(block_no / (BLOCK_SIZE * 8) + sblock->bitmap_start);
    bitmap_clear((BitmapCell *)bitmap_block->data, block_no % (BLOCK_SIZE * 8));
    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);
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

void evict_block() {
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
