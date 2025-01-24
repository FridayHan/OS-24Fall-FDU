#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>

// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    for (int i = 0; i < NFILE; i++) {
        ftable.files[i].ref = 0;  // 初始化每个文件对象的引用计数为0
        ftable.files[i].type = FD_NONE;  // 将文件类型初始化为FD_NONE
        ftable.files[i].off = 0;  // 偏移量初始化为0
        ftable.files[i].readable = false;  // 默认不可读
        ftable.files[i].writable = false;  // 默认不可写
    }
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for (int i = 0; i < NOFILE; i++) {
        oftable->ofiles[i] = NULL;
    }
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    for (int i = 0; i < NFILE; i++) {
        if (ftable.files[i].ref == 0) {
            ftable.files[i].ref = 1;
            return &ftable.files[i];
        }
    }
    printk("file_alloc: no free file\n");
    /* (Final) TODO END */
    return NULL;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    f->ref++;
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* (Final) TODO BEGIN */
    if (f == NULL) {
        return;  // 如果文件是空指针，直接返回
    }

    f->ref--;  // 减少引用计数

    if (f->ref == 0) {  // 如果引用计数为 0，关闭文件
        if (f->type == FD_INODE) {
            inodes.put(NULL, f->ip);  // 添加 OpContext* 参数
        } else if (f->type == FD_PIPE) {
            pipe_close(f->pipe, 1);  // 添加 writable 参数
        }
        f->type = FD_NONE;  // 设置文件类型为无效状态
        f->off = 0;  // 重置偏移量
    }
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        stati(f->ip, st);  // 修改为直接调用 stati 函数
        inodes.unlock(f->ip);
        return 0;
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n)
{
    /* (Final) TODO BEGIN */
    printk("file_read: READING type=%d, off=%lld, n=%lld\n", f->type, f->off, n);
    if (f->readable == false)
    {
        printk("file_read: file is not readable\n");
        return -1;
    }

    if (f->type == FD_INODE)
    {
        inodes.lock(f->ip);
        isize result = inodes.read(f->ip, (u8*)addr, f->off, n);
        if (result > 0)
        {
            f->off += result;
        }
        inodes.unlock(f->ip);
        return result;
    }
    else if (f->type == FD_PIPE)
    {
        isize result = pipe_read(f->pipe, (u64)addr, n);
        // if (result > 0)
        // {
        //     f->off += result;
        // }
        return result;
    }
    printk("file_read: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n)
{
    /* (Final) TODO BEGIN */
    isize total_written = 0;
    if (f->type == FD_INODE)
    {
        usize max_write_size = MIN(INODE_MAX_BYTES - f->off, (usize)n);
        usize bytes_written = 0;

        while (bytes_written < max_write_size) {
            usize write_chunk_size = MIN(
                max_write_size - bytes_written,
                (usize)((OP_MAX_NUM_BLOCKS - 2) * BLOCK_SIZE)  // 为了避免超出操作块的最大大小，稍微减少一些
            );

            OpContext op_context;
            bcache.begin_op(&op_context);

            inodes.lock(f->ip);

            if (inodes.write(&op_context, f->ip, (u8 *)(addr + bytes_written), f->off, write_chunk_size) != write_chunk_size) {
                inodes.unlock(f->ip);
                bcache.end_op(&op_context);
                return -1;
            }

            inodes.unlock(f->ip);
            bcache.end_op(&op_context);

            f->off += write_chunk_size;
            bytes_written += write_chunk_size;
            total_written += write_chunk_size;
        }

        return total_written;
    }
    else if (f->type == FD_PIPE)
    {
        isize result = pipe_write(f->pipe, (u64)addr, n);
        // if (result > 0)
        // {
        //     f->off += result;
        // }
        return result;
    }
    printk("file_write: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}
