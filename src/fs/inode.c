#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/console.h>

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
static INLINE usize to_block_no(usize inode_no)
{
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no)
{
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block)
{
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache)
{
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;
    printk("ROOT_INODE_NO:%d\n", ROOT_INODE_NO);
    printk("sblock->num_inodes:%d\n", sblock->num_inodes);
    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode)
{
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type)
{
    ASSERT(type != INODE_INVALID);
    usize inum = 1;
    while (inum < sblock->num_inodes)
    {
        usize block_no = to_block_no(inum);
        Block* block = cache->acquire(block_no);
        InodeEntry* entry = get_entry(block, inum);
        if (entry->type == INODE_INVALID)
        {
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
static void inode_lock(Inode* inode)
{
    ASSERT(inode->rc.count > 0);
    acquire_sleeplock(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode)
{
    ASSERT(inode->rc.count > 0);
    release_sleeplock(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write)
{
    if (do_write)
    {
        ASSERT(inode->valid);
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);

        memcpy(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, block);
        cache->release(block);
    }
    else
    {
        if (!inode->valid)
        {
            Block* block = cache->acquire(to_block_no(inode->inode_no));
            InodeEntry* entry = get_entry(block, inode->inode_no);
            memcpy(&inode->entry, entry, sizeof(InodeEntry));
            inode->valid = true;
            cache->release(block);
        }
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no)
{
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    Inode* inode;
    _for_in_list(node, &head)
    {
        inode = container_of(node, Inode, node);
        if (inode->inode_no == inode_no)
        {
            increment_rc(&inode->rc);
            release_spinlock(&lock);
            return inode;
        }
    }

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
static void inode_clear(OpContext* ctx, Inode* inode)
{
    ASSERT(inode->rc.count > 0);

    InodeEntry* entry = &inode->entry;
    for (usize i = 0; i < INODE_NUM_DIRECT; i++)
    {
        if (entry->addrs[i])
        {
            cache->free(ctx, entry->addrs[i]);
        }
    }

    if (entry->indirect)
    {
        Block* block = cache->acquire(entry->indirect);
        u32* addrs = get_addrs(block);
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++)
        {
            if (addrs[i])
            {
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
static Inode* inode_share(Inode* inode)
{
    ASSERT(inode->rc.count > 0);
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode)
{
    ASSERT(inode->rc.count > 0);
    acquire_spinlock(&lock);
    if (inode->rc.count == 1 && inode->entry.num_links == 0)
    {
        inode_lock(inode);
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, true);
        inode_unlock(inode);
        _detach_from_list(&inode->node);
        kfree(inode);
    }
    else
    {
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
static usize inode_map(OpContext* ctx, Inode* inode, usize offset, bool* modified)
{
    InodeEntry* entry = &inode->entry;
    usize block_idx = offset / BLOCK_SIZE;
    usize block_no = 0;
    if (block_idx < INODE_NUM_DIRECT)
    {
        block_no = entry->addrs[block_idx];
        if (block_no == 0)
        {
            if (ctx == NULL) return 0;

            block_no = cache->alloc(ctx);
            entry->addrs[block_idx] = block_no;
            *modified = true;
        }
    }
    else
    {
        if (entry->indirect == 0)
        {
            if (ctx == NULL) return 0;
            entry->indirect = cache->alloc(ctx);
            *modified = true;
        }

        IndirectBlock* indirect_block = (IndirectBlock*)cache->acquire(entry->indirect);
        usize indirect_idx = block_idx - INODE_NUM_DIRECT;
        block_no = get_addrs((Block*)indirect_block)[indirect_idx];
        if (block_no == 0)
        {
            if (ctx == NULL) return 0;
            block_no = cache->alloc(ctx);
            get_addrs((Block*)indirect_block)[indirect_idx] = block_no;
            *modified = true;
        }
        cache->release((Block*)indirect_block);
    }

    return block_no;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count)
{
    if (inode->entry.type == INODE_DEVICE)
    {
        return console_read(inode, (char *)dest, count);
    }
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    usize read_bytes = offset;
    while (read_bytes < end)
    {
        usize block_no = inode_map(NULL, inode, read_bytes, NULL);
        if (block_no == 0) PANIC();

        usize block_offset = read_bytes % BLOCK_SIZE;
        usize bytes_to_read = MIN(BLOCK_SIZE - block_offset, end - read_bytes);

        Block* block = cache->acquire(block_no);
        memcpy(dest + read_bytes - offset, block->data + block_offset, bytes_to_read);
        cache->release(block);

        read_bytes += bytes_to_read;
    }

    return read_bytes - offset;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx, Inode* inode, u8* src, usize offset, usize count)
{
    if(inode->entry.type == INODE_DEVICE)
    {
        return console_write(inode, (char *)src, count);
    }
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    usize written_bytes = offset;
    while (written_bytes < end)
    {
        bool modified;
        usize block_no = inode_map(ctx, inode, written_bytes, &modified);
        if (block_no == 0) PANIC();

        usize block_offset = written_bytes % BLOCK_SIZE;
        usize bytes_to_write = MIN(BLOCK_SIZE - block_offset, end - written_bytes);

        Block* block = cache->acquire(block_no);
        memcpy(block->data + block_offset, src + written_bytes - offset, bytes_to_write);
        cache->sync(ctx, block);
        cache->release(block);

        written_bytes += bytes_to_write;
    }
    if (end > entry->num_bytes)
    {
        entry->num_bytes = end;
        inode_sync(ctx, inode, true);
    }

    return written_bytes - offset;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index)
{
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    DirEntry dir_entry;
    usize offset = 0;
    usize idx = 0;
    while (offset < entry->num_bytes)
    {
        inode_read(inode, (u8*)&dir_entry, offset, sizeof(DirEntry));
        if (dir_entry.inode_no != 0 && strncmp(dir_entry.name, name, FILE_NAME_MAX_LENGTH) == 0)
        {
            if (index != NULL)
                *index = idx;
            return dir_entry.inode_no;
        }
        idx++;
        offset += sizeof(DirEntry);
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx, Inode* inode, const char* name, usize inode_no)
{
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    usize index;
    if (inode_lookup(inode, name, &index) != 0) return -1;

    DirEntry dir_entry;
    u32 offset;
    for (offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry))
    {
        inode_read(inode, (u8*)&dir_entry, offset, sizeof(DirEntry));
        if (dir_entry.inode_no == 0) break;
    }

    dir_entry.inode_no = inode_no;
    memmove(dir_entry.name, name, FILE_NAME_MAX_LENGTH);

    usize num = inode_write(ctx, inode, (u8*)&dir_entry, offset, sizeof(DirEntry));
    ASSERT(num == sizeof(DirEntry));

    return offset / sizeof(DirEntry);
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index)
{
    ASSERT(inode->entry.type == INODE_DIRECTORY);
    usize offset = index * sizeof(DirEntry);
    if (offset >= inode->entry.num_bytes) return;

    DirEntry dir_entry;
    memset(&dir_entry, 0, sizeof(DirEntry));

    usize num = inode_write(ctx, inode, (u8*)&dir_entry, index, sizeof(DirEntry));
    ASSERT(num == sizeof(DirEntry));

    while(inode->entry.num_bytes == index + sizeof(DirEntry))
    {
        inode_read(inode, (u8*)&dir_entry, index, sizeof(DirEntry));
        if (!dir_entry.inode_no)
        {
            inode->entry.num_bytes -= sizeof(DirEntry);
        }
        if (index)
        {
            index -= sizeof(DirEntry);
        }
    }
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

/**
    @brief read the next path element from `path` into `name`.
    
    @param[out] name next path element.

    @return const char* a pointer offseted in `path`, without leading `/`. If no
    name to remove, return NULL.

    @example 
    skipelem("a/bb/c", name) = "bb/c", setting name = "a",
    skipelem("///a//bb", name) = "bb", setting name = "a",
    skipelem("a", name) = "", setting name = "a",
    skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char* skipelem(const char* path, char* name)
{
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else
    {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/**
    @brief look up and return the inode for `path`.

    If `nameiparent`, return the inode for the parent and copy the final
    path element into `name`.
    
    @param path a relative or absolute path. If `path` is relative, it is
    relative to the current working directory of the process.

    @param[out] name the final path element if `nameiparent` is true.

    @return Inode* the inode for `path` (or its parent if `nameiparent` is true), 
    or NULL if such inode does not exist.

    @example
    namex("/a/b", false, name) = inode of b,
    namex("/a/b", true, name) = inode of a, setting name = "b",
    namex("/", true, name) = NULL (because "/" has no parent!)
 */
static Inode* namex(const char* path, bool nameiparent, char* name, OpContext* ctx)
{
    /* (Final) TODO BEGIN */
    Inode *ip, *next;

    if (*path == '/') ip = inodes.root;
    else ip = inodes.share(thisproc()->cwd);

    while ((path = skipelem(path, name)) != 0)
    {
        inodes.lock(ip);
        if (ip->entry.type != INODE_DIRECTORY)
        {
            printk("namex: not a directory: %s\n", name);
            inodes.unlock(ip);
            inodes.put(ctx, ip);
            return NULL;
        }

        if (nameiparent && *path == '\0')
        {
            inodes.unlock(ip);
            return ip;
        }

        usize inode_no = inodes.lookup(ip, name, NULL);
        if (inode_no == 0)
        {
            inodes.unlock(ip);
            inodes.put(ctx, ip);
            return NULL;
        }

        next = inodes.get(inode_no);
        inodes.unlock(ip);
        ip = next;
    }

    if (nameiparent)
    {
        inodes.put(ctx, ip);
        return NULL;
    }

    return ip;
    /* (Final) TODO END */
}

Inode* namei(const char* path, OpContext* ctx)
{
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, false, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx)
{
    return namex(path, true, name, ctx);
}

/**
    @brief get the stat information of `ip` into `st`.
    
    @note the caller must hold the lock of `ip`.
 */
void stati(Inode* ip, struct stat* st)
{
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type)
    {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}