#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>

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
    for (int i = 0; i < NFILE; i++) {
        oftable->fds[i] = -1;  // 将所有文件描述符初始化为无效的 -1
    }
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    for (int i = 0; i < NFILE; i++) {
        if (ftable.files[i].ref == 0) {  // 找到一个未被使用的文件
            ftable.files[i].ref = 1;  // 设置引用计数为 1
            return &ftable.files[i];  // 返回该文件对象
        }
    }
    printk("file_alloc: no free file\n");
    return NULL;
    /* (Final) TODO END */
    return 0;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    f->ref++;  // 引用计数增加
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
            inode_put(f->ip);  // 关闭硬盘文件时，减少 inode 的引用计数
        } else if (f->type == FD_PIPE) {
            pipe_close(f->pipe);  // 关闭管道文件
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
        return stati(f->ip, st);  // 如果是硬盘文件，通过 inode 获取文件的元数据
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE) {
        return inode_read(f->ip, addr, n, &f->off);  // 从硬盘文件读取数据
    } else if (f->type == FD_PIPE) {
        return pipe_read(f->pipe, addr, n);  // 从管道文件读取数据
    }
    printk("file_read: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE) {
        return inode_write(f->ip, addr, n, &f->off);  // 向硬盘文件写入数据
    } else if (f->type == FD_PIPE) {
        return pipe_write(f->pipe, addr, n);  // 向管道文件写入数据
    }
    printk("file_write: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}