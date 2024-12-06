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
    init_bcache(sblock, &bcache);
    bcache.begin_op(ctx);
    usize block_no = cache_alloc(ctx);
    Block* block = bcache.acquire(block_no);

    memset(block->data, 0, BLOCK_SIZE);
    printk("block->data: %d\n", block->data);
    InodeEntry* entry = (InodeEntry*)block->data;
    for (usize i = 0; i < INODE_PER_BLOCK; i++) {
        if (entry[i].type == INODE_INVALID) {
            entry[i].type = type;
            entry[i].num_links = 1;
            entry[i].num_bytes = 0;
            entry[i].indirect = 0;
            // bcache.sync(ctx, block);
            bcache.release(block);
            bcache.end_op(ctx);
            printk("entry: %d\n", entry[i].type);
            return block_no * INODE_PER_BLOCK + i;
        }
    }

    PANIC();  // If we couldn't find a free inode slot
    return 0;
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
        // Write inode back to disk
        ASSERT(inode->valid);  // The inode must be valid before writing
        Block* block = bcache.acquire(to_block_no(inode->inode_no));  // Acquire the block
        InodeEntry* entry = get_entry(block, inode->inode_no);

        memcpy(entry, &inode->entry, sizeof(InodeEntry));  // Copy the inode entry
        bcache.sync(ctx, block);  // Sync the block to disk
        bcache.release(block);  // Release the block
    } else {
        // Read inode from disk if not valid
        if (!inode->valid) {
            Block* block = bcache.acquire(to_block_no(inode->inode_no));  // Acquire the block
            InodeEntry* entry = get_entry(block, inode->inode_no);
            memcpy(&inode->entry, entry, sizeof(InodeEntry));  // Copy the inode data
            inode->valid = true;  // Mark inode as valid
            bcache.release(block);  // Release the block
        }
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    // TODO

    // Traverse the list to find the inode, or create it if necessary
    Inode* inode = NULL;
    ListNode* node;
    _for_in_list(node, &head) {
        inode = container_of(node, Inode, node);
        if (inode->inode_no == inode_no) {
            inode->rc.count++;  // Increment reference count
            release_spinlock(&lock);
            return inode;
        }
    }

    // If inode is not found, create a new one
    inode = (Inode*)kalloc(sizeof(Inode));  // Allocate memory for inode
    init_inode(inode);  // Initialize the inode structure
    inode->inode_no = inode_no;  // Set the inode number
    inode->valid = false;  // Mark as invalid initially
    _insert_into_list(&head, &inode->node);  // Add inode to the inode list

    inode->rc.count = 1;  // Set reference count to 1
    release_spinlock(&lock);
    return inode;

    return NULL;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);  // Ensure the inode is valid

    // Free the direct blocks
    for (usize i = 0; i < INODE_NUM_DIRECT; i++) {
        if (inode->entry.addrs[i] != 0) {
            bcache.free(ctx, inode->entry.addrs[i]);  // Free the block
            inode->entry.addrs[i] = 0;  // Reset the address
        }
    }

    // Free the indirect block if used
    if (inode->entry.indirect != 0) {
        Block* block = bcache.acquire(inode->entry.indirect);  // Acquire the indirect block
        u32* addrs = get_addrs(block);
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
            if (addrs[i] != 0) {
                bcache.free(ctx, addrs[i]);  // Free each address block
            }
        }
        bcache.free(ctx, inode->entry.indirect);  // Free the indirect block itself
        inode->entry.indirect = 0;  // Reset indirect address
    }

    inode->entry.num_bytes = 0;  // Reset the file size to zero
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);  // Ensure the inode is valid
    inode->rc.count++;  // Increment the reference count
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    ASSERT(inode->rc.count > 0);  // Ensure the inode is valid

    inode->rc.count--;  // Decrement reference count

    // If no one needs this inode anymore, free it
    if (inode->rc.count == 0) {
        // Remove the inode from the list
        _detach_from_list(&inode->node);

        // Clear the inode's content and free the inode structure
        inode_clear(ctx, inode);
        kfree(inode);  // Free the inode itself
    }
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

    // Calculate the block index
    usize block_idx = offset / BLOCK_SIZE;
    if (block_idx < INODE_NUM_DIRECT) {
        // Direct block
        if (entry->addrs[block_idx] == 0) {
            if (ctx == NULL) {
                return 0;
            }

            // Allocate a new block and mark it as modified
            usize block_no = bcache.alloc(ctx);
            entry->addrs[block_idx] = block_no;
            *modified = true;
        }
        return entry->addrs[block_idx];
    } else if (block_idx < INODE_NUM_DIRECT + INODE_NUM_INDIRECT) {
        // Indirect block
        if (entry->indirect == 0) {
            if (ctx == NULL) {
                return 0;
            }

            // Allocate indirect block
            entry->indirect = bcache.alloc(ctx);
            *modified = true;
        }

        // Read the indirect block and check for allocation
        IndirectBlock* indirect_block = (IndirectBlock*)bcache.acquire(entry->indirect);
        usize indirect_idx = block_idx - INODE_NUM_DIRECT;
        if (indirect_block->addrs[indirect_idx] == 0) {
            if (ctx == NULL) {
                return 0;
            }

            // Allocate a new data block
            indirect_block->addrs[indirect_idx] = bcache.alloc(ctx);
            *modified = true;
        }
        bcache.release((Block*)indirect_block);
        return indirect_block->addrs[indirect_idx];
    }

    return 0;  // Error, out of bounds
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
    // Ensure that we do not read past the end of the file
    if (end > entry->num_bytes) {
        count = entry->num_bytes - offset;
    }

    // Read data block by block
    usize read_bytes = 0;
    while (count > 0) {
        // Find the block number containing the offset
        usize block_no = inode_map(NULL, inode, offset, NULL);
        if (block_no == 0) {
            break;
        }

        // Calculate the number of bytes we can read from this block
        usize block_offset = offset % BLOCK_SIZE;
        usize bytes_to_read = BLOCK_SIZE - block_offset;
        if (bytes_to_read > count) {
            bytes_to_read = count;
        }

        // Read from the block
        Block* block = bcache.acquire(block_no);
        memcpy(dest + read_bytes, block->data + block_offset, bytes_to_read);
        bcache.release(block);

        read_bytes += bytes_to_read;
        offset += bytes_to_read;
        count -= bytes_to_read;
    }

    return read_bytes;
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
    if (end > INODE_MAX_BYTES) {
        return 0;
    }

    // Write data block by block
    usize written_bytes = 0;
    while (count > 0) {
        usize block_no = inode_map(ctx, inode, offset, true);
        if (block_no == 0) {
            return written_bytes;
        }

        // Calculate how many bytes we can write
        usize block_offset = offset % BLOCK_SIZE;
        usize bytes_to_write = BLOCK_SIZE - block_offset;
        if (bytes_to_write > count) {
            bytes_to_write = count;
        }

        // Acquire block and write data
        Block* block = bcache.acquire(block_no);
        memcpy(block->data + block_offset, src + written_bytes, bytes_to_write);
        if (true) {
            bcache.sync(ctx, block);
        }
        bcache.release(block);

        written_bytes += bytes_to_write;
        offset += bytes_to_write;
        count -= bytes_to_write;
    }

    return written_bytes;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    // Iterate over all directory entries
    DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    for (usize i = 0; i < INODE_MAX_BLOCKS; i++) {
        if (strcmp(dir_entry[i].name, name) == 0) {
            *index = i;
            return dir_entry[i].inode_no;
        }
    }

    return 0; // Not found
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    // Find an empty spot in the directory
    DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    for (usize i = 0; i < INODE_MAX_BLOCKS; i++) {
        if (dir_entry[i].inode_no == 0) {
            // Insert new entry
            strncpy(dir_entry[i].name, name, FILE_NAME_MAX_LENGTH);
            dir_entry[i].inode_no = inode_no;

            // Sync changes to disk
            bcache.sync(ctx, inode);

            return i;
        }
    }

    // If directory is full, grow it (simplified)
    inode->entry.num_bytes += BLOCK_SIZE;
    bcache.sync(ctx, inode); // Sync after growing

    return inode_insert(ctx, inode, name, inode_no); // Try again after growing

}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // Remove the directory entry by shifting
    DirEntry* dir_entry = (DirEntry*)inode->entry.addrs;
    for (usize i = index; i < INODE_MAX_BLOCKS - 1; i++) {
        dir_entry[i] = dir_entry[i + 1];
    }

    dir_entry[INODE_MAX_BLOCKS - 1].inode_no = 0;  // Clear the last entry

    // Sync changes to disk
    bcache.sync(ctx, inode);

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