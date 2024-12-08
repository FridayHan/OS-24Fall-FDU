#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    usize inum = 1;
    // printk("sblock->num_inodes: %d\n", sblock->num_inodes);

    while (inum < sblock->num_inodes) {
        usize block_no = to_block_no(inum);
        Block* block = cache->acquire(block_no);
        InodeEntry* entry = get_entry(block, inum);
        if (entry->type == INODE_INVALID) {
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, block);
            cache->release(block);
            return inum;
        }
        cache->release(block);
        inum++;
    }

    PANIC();
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    acquire_sleeplock(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    release_sleeplock(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    if (do_write) {
        ASSERT(inode->valid);
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);

        memcpy(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, block);
        cache->release(block);
    } else {
        if (!inode->valid) {
            Block* block = cache->acquire(to_block_no(inode->inode_no));
            InodeEntry* entry = get_entry(block, inode->inode_no);
            memcpy(&inode->entry, entry, sizeof(InodeEntry));
            inode->valid = true;
            cache->release(block);
        }
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    // TODO
    Inode* inode;
    _for_in_list(node, &head) {
        inode = container_of(node, Inode, node);
        if (inode->inode_no == inode_no) {
            increment_rc(&inode->rc);
            release_spinlock(&lock);
            return inode;
        }
    }

    // NOT FOUND
    inode = (Inode*)kalloc(sizeof(Inode));
    init_inode(inode);
    inode->inode_no = inode_no;
    increment_rc(&inode->rc);
    
    inode_lock(inode);
    inode_sync(NULL, inode, false);
    inode_unlock(inode);

    inode->valid = true;
    _insert_into_list(&head, &inode->node);

    release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);

    InodeEntry* entry = &inode->entry;
    for (usize i = 0; i < INODE_NUM_DIRECT; i++) {
        if (entry->addrs[i]) {
            cache->free(ctx, entry->addrs[i]);
        }
    }

    if (entry->indirect) {
        Block* block = cache->acquire(entry->indirect);
        u32* addrs = get_addrs(block);
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
            if (addrs[i]) {
                cache->free(ctx, addrs[i]);
            }
        }
        cache->release(block);
        cache->free(ctx, entry->indirect);
    }

    entry->indirect = 0;
    entry->num_bytes = 0;
    memset(entry->addrs, 0, sizeof(entry->addrs));
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);

    acquire_spinlock(&lock);
    if (inode->rc.count == 1 && inode->entry.num_links == 0) {
        inode_lock(inode);
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, true);
        inode_unlock(inode);
        _detach_from_list(&inode->node);
        kfree(inode);
    } else {
        decrement_rc(&inode->rc);
    }
    release_spinlock(&lock);
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    *modified = false;
    InodeEntry* entry = &inode->entry;
    usize block_idx = offset / BLOCK_SIZE;
    if (block_idx < INODE_NUM_DIRECT) {
        if (entry->addrs[block_idx] == 0) {
            if (ctx == NULL) {
                return 0;
            }

            usize block_no = cache->alloc(ctx);
            entry->addrs[block_idx] = block_no;
            *modified = true;
        }
        return entry->addrs[block_idx];
    } else {
        if (entry->indirect == 0) {
            if (ctx == NULL) {
                return 0;
            }

            entry->indirect = cache->alloc(ctx);
            *modified = true;
        }

        IndirectBlock* indirect_block = (IndirectBlock*)cache->acquire(entry->indirect);
        usize indirect_idx = block_idx - INODE_NUM_DIRECT;
        if (indirect_block->addrs[indirect_idx] == 0) {
            if (ctx == NULL) {
                return 0;
            }

            indirect_block->addrs[indirect_idx] = cache->alloc(ctx);
            *modified = true;
        }
        cache->release((Block*)indirect_block);
        return indirect_block->addrs[indirect_idx];
    }

    return 0;  // ERROR
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    usize read_bytes = offset;
    while (read_bytes < end) {
        usize block_no = inode_map(NULL, inode, read_bytes, NULL);
        if (block_no == 0) {
            PANIC();
        }

        usize block_offset = read_bytes % BLOCK_SIZE;
        usize bytes_to_read = MIN(BLOCK_SIZE - block_offset, end - read_bytes);

        Block* block = cache->acquire(block_no);
        memcpy(dest + read_bytes, block->data + block_offset, bytes_to_read);
        cache->release(block);

        read_bytes += bytes_to_read;
    }

    return read_bytes - offset;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    // if (end > INODE_MAX_BYTES) {
    //     return 0;
    // }

    usize written_bytes = 0;
    while (written_bytes < end) {
        bool modified;
        usize block_no = inode_map(ctx, inode, offset, &modified);
        if (block_no == 0) {
            PANIC();
        }

        usize block_offset = written_bytes % BLOCK_SIZE;
        usize bytes_to_write = MIN(BLOCK_SIZE - block_offset, end - written_bytes);

        Block* block = cache->acquire(block_no);
        memcpy(block->data + block_offset, src + written_bytes - offset, bytes_to_write);
        cache->sync(ctx, block);
        cache->release(block);

        written_bytes += bytes_to_write;
    }
    if (end > entry->num_bytes) {
        entry->num_bytes = end;
        inode_sync(ctx, inode, true);
    }

    return written_bytes - offset;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) { // TODO: copied
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    usize offset = 0;
    usize idx = 0;
    while (offset < entry->num_bytes) {
        inode_read(inode, (u8*)&dir_entry, offset, sizeof(DirEntry));
        if (dir_entry->inode_no != 0 && strncmp(dir_entry->name, name, FILE_NAME_MAX_LENGTH) == 0) {
            if (index != NULL)
                *index = idx;
            return dir_entry->inode_no;
        }
        idx++;
        offset += sizeof(DirEntry);
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) { // TODO: copied
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    // // Find an empty spot in the directory
    // DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    // for (usize i = 0; i < INODE_MAX_BLOCKS; i++) {
    //     if (dir_entry[i].inode_no == 0) {
    //         // Insert new entry
    //         strncpy(dir_entry[i].name, name, FILE_NAME_MAX_LENGTH);
    //         dir_entry[i].inode_no = inode_no;

    //         // Sync changes to disk
    //         cache->sync(ctx, inode);

    //         return i;
    //     }
    // }

    // // If directory is full, grow it (simplified)
    // inode->entry.num_bytes += BLOCK_SIZE;
    // cache->sync(ctx, inode); // Sync after growing

    // return inode_insert(ctx, inode, name, inode_no); // Try again after growing

    usize index;
    if (inode_lookup(inode, name, &index) != 0) {
        return -1;
    }

    DirEntry de;
    u32 offset;
    for (offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)) {
        inode_read(inode, (u8*)&de, offset, sizeof(DirEntry));
        if (de.inode_no == 0) {
            break;
        }
    }

    de.inode_no = inode_no;
    memmove(de.name, name, FILE_NAME_MAX_LENGTH);

    usize num = inode_write(ctx, inode, (u8*)&de, offset, sizeof(DirEntry));
    ASSERT(num == sizeof(DirEntry));

    return offset / sizeof(DirEntry);
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) { // TODO: copied
    // TODO
    ASSERT(inode->entry.type == INODE_DIRECTORY);
    usize offset = index * sizeof(DirEntry);
    if (offset >= inode->entry.num_bytes) {
        return;  // Index out of bounds
    }

    DirEntry de;
    memset(&de, 0, sizeof(DirEntry));

    usize num = inode_write(ctx, inode, (u8*)&de, index, sizeof(DirEntry));
    ASSERT(num == sizeof(DirEntry));

    while(inode->entry.num_bytes == index + sizeof(DirEntry)) {
        inode_read(inode, (u8*)&de, index, sizeof(DirEntry));
        if (!de.inode_no) {
            inode->entry.num_bytes -= sizeof(DirEntry);
        }
        if (index) {
            index -= sizeof(DirEntry);
        }
    }

    

    // // Remove the directory entry by shifting
    // DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    // for (usize i = index; i < INODE_MAX_BLOCKS - 1; i++) {
    //     dir_entry[i] = dir_entry[i + 1];
    // }

    // dir_entry[INODE_MAX_BLOCKS - 1].inode_no = 0;  // Clear the last entry

    // // Sync changes to disk
    // cache->sync(ctx, inode);

}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};