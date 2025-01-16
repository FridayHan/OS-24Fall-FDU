//
// File-system system calls implementation.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <stddef.h>

#include "syscall.h"
#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

struct iovec {
    void *iov_base; /* Starting address. */
    usize iov_len; /* Number of bytes to transfer. */
};

/** 
 * Get the file object by fd. Return null if the fd is invalid.
 */
static struct file *fd2file(int fd)
{
    /* (Final) TODO BEGIN */
    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
        return NULL;  // 文件描述符无效

    struct file *f = current->files[fd];  // 假设 current 代表当前进程，files 是文件描述符表
    if (!f)
        return NULL;  // 文件描述符未分配文件对象
    return f;
    /* (Final) TODO END */
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
int fdalloc(struct file *f)
{
    /* (Final) TODO BEGIN */
    // 查找第一个空闲的文件描述符位置
    for (int fd = 0; fd < MAX_FILES_PER_PROCESS; fd++) {
        if (current->files[fd] == NULL) {
            current->files[fd] = f;  // 将文件对象关联到文件描述符
            return fd;  // 返回分配的文件描述符
        }
    }
    /* (Final) TODO END */
    return -1;
}

define_syscall(ioctl, int fd, u64 request)
{
    // 0x5413 is TIOCGWINSZ (I/O Control to Get the WINdow SIZe, a magic request
    // to get the stdin terminal size) in our implementation. Just ignore it.
    ASSERT(request == 0x5413);
    (void)fd;
    return 0;
}

define_syscall(mmap, void *addr, int length, int prot, int flags, int fd,
               int offset)
{
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}

define_syscall(munmap, void *addr, size_t length)
{
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}

define_syscall(dup, int fd)
{
    struct file *f = fd2file(fd);
    if (!f)
        return -1;
    fd = fdalloc(f);
    if (fd < 0)
        return -1;
    file_dup(f);
    return fd;
}

define_syscall(read, int fd, char *buffer, int size)
{
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_writeable(buffer, size))
        return -1;
    return file_read(f, buffer, size);
}

define_syscall(write, int fd, char *buffer, int size)
{
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_readable(buffer, size))
        return -1;
    return file_write(f, buffer, size);
}

define_syscall(writev, int fd, struct iovec *iov, int iovcnt)
{
    struct file *f = fd2file(fd);
    struct iovec *p;
    if (!f || iovcnt <= 0 || !user_readable(iov, sizeof(struct iovec) * iovcnt))
        return -1;
    usize tot = 0;
    for (p = iov; p < iov + iovcnt; p++) {
        if (!user_readable(p->iov_base, p->iov_len))
            return -1;
        tot += file_write(f, p->iov_base, p->iov_len);
    }
    return tot;
}

define_syscall(close, int fd)
{
    /* (Final) TODO BEGIN */
    struct file *f = fd2file(fd);
    if (!f)
        return -1;

    // 清理文件描述符资源
    fd2file(fd) = NULL;  // 将文件描述符的文件对象指针设为 NULL，表示该描述符已关闭

    // 执行文件关闭操作
    file_close(f);

    // 释放文件的内存资源
    file_put(f);
    /* (Final) TODO END */
    return 0;
}

define_syscall(fstat, int fd, struct stat *st)
{
    struct file *f = fd2file(fd);
    if (!f || !user_writeable(st, sizeof(*st)))
        return -1;
    return file_stat(f, st);
}

define_syscall(newfstatat, int dirfd, const char *path, struct stat *st,
               int flags)
{
    if (!user_strlen(path, 256) || !user_writeable(st, sizeof(*st)))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_fstatat: dirfd unimplemented\n");
        return -1;
    }
    if (flags != 0) {
        printk("sys_fstatat: flags unimplemented\n");
        return -1;
    }

    Inode *ip;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    stati(ip, st);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);

    return 0;
}

static int isdirempty(Inode *dp)
{
    usize off;
    DirEntry de;

    for (off = 2 * sizeof(de); off < dp->entry.num_bytes; off += sizeof(de)) {
        if (inodes.read(dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
            PANIC();
        if (de.inode_no != 0)
            return 0;
    }
    return 1;
}

define_syscall(unlinkat, int fd, const char *path, int flag)
{
    ASSERT(fd == AT_FDCWD && flag == 0);
    Inode *ip, *dp;
    DirEntry de;
    char name[FILE_NAME_MAX_LENGTH];
    usize off;
    if (!user_strlen(path, 256))
        return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((dp = nameiparent(path, name, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(dp);

    // Cannot unlink "." or "..".
    if (strncmp(name, ".", FILE_NAME_MAX_LENGTH) == 0 ||
        strncmp(name, "..", FILE_NAME_MAX_LENGTH) == 0)
        goto bad;

    usize inumber = inodes.lookup(dp, name, &off);
    if (inumber == 0)
        goto bad;
    ip = inodes.get(inumber);
    inodes.lock(ip);

    if (ip->entry.num_links < 1)
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY && !isdirempty(ip)) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (inodes.write(&ctx, dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY) {
        dp->entry.num_links--;
        inodes.sync(&ctx, dp, true);
    }
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    ip->entry.num_links--;
    inodes.sync(&ctx, ip, true);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

bad:
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    bcache.end_op(&ctx);
    return -1;
}

/**
    @brief create an inode at `path` with `type`.

    If the inode exists, just return it.

    If `type` is directory, you should also create "." and ".." entries and link
   them with the new inode.

    @note BE careful of handling error! You should clean up ALL the resources
   you allocated and free ALL acquired locks when error occurs. e.g. if you
   allocate a new inode "/my/dir", but failed to create ".", you should free the
   inode "/my/dir" before return.

    @see `nameiparent` will find the parent directory of `path`.

    @return Inode* the created inode, or NULL if failed.
 */
Inode *create(const char *path, short type, short major, short minor,
              OpContext *ctx)
{
    /* (Final) TODO BEGIN */
    // 检查路径是否合法
    if (!user_strlen(path, 256))
        return NULL;

    // 查找路径父目录和文件名
    char name[FILE_NAME_MAX_LENGTH];
    Inode *dp = NULL;
    if ((dp = nameiparent(path, name, ctx)) == NULL)
        return NULL;  // 无法找到父目录，返回 NULL

    // 锁住父目录
    inodes.lock(dp);

    // 检查路径上是否已有文件或目录
    Inode *ip;
    if ((ip = inodes.lookup(dp, name, NULL)) != NULL) {
        inodes.unlock(dp);  // 路径已存在，解锁父目录
        inodes.put(ctx, dp);  // 释放父目录
        return ip;  // 返回现有的 Inode
    }

    // 创建新 inode
    ip = inodes.alloc(type, major, minor);  // 分配新的 inode
    if (!ip) {
        inodes.unlock(dp);
        inodes.put(ctx, dp);
        return NULL;  // 分配 inode 失败，返回 NULL
    }

    // 处理目录创建的特殊情况
    if (type == INODE_DIRECTORY) {
        // 创建 "." 和 ".." 目录项
        DirEntry dot_entry = { .inode_no = ip->entry.inode_no, .name = "." };
        DirEntry dotdot_entry = { .inode_no = dp->entry.inode_no, .name = ".." };

        // 添加 "." 和 ".." 目录项
        if (inodes.write(ctx, ip, (u8 *)&dot_entry, 0, sizeof(dot_entry)) != sizeof(dot_entry) ||
            inodes.write(ctx, ip, (u8 *)&dotdot_entry, sizeof(dot_entry), sizeof(dotdot_entry)) != sizeof(dotdot_entry)) {
            inodes.free(ip);  // 释放创建的 inode
            inodes.unlock(dp);
            inodes.put(ctx, dp);
            return NULL;  // 目录项写入失败，返回 NULL
        }
    }

    // 将新 inode 加入父目录
    DirEntry de = { .inode_no = ip->entry.inode_no, .name = name };
    if (inodes.write(ctx, dp, (u8 *)&de, dp->entry.num_bytes, sizeof(de)) != sizeof(de)) {
        inodes.free(ip);
        inodes.unlock(dp);
        inodes.put(ctx, dp);
        return NULL;  // 写入目录项失败，释放 inode 并返回 NULL
    }

    // 更新父目录的链接计数
    dp->entry.num_links++;
    inodes.sync(ctx, dp, true);
    inodes.unlock(dp);

    // 返回新创建的 inode
    inodes.put(ctx, dp);
    return ip;
    /* (Final) TODO END */
    return 0;
}

define_syscall(openat, int dirfd, const char *path, int omode)
{
    int fd;
    struct file *f;
    Inode *ip;

    if (!user_strlen(path, 256))
        return -1;

    if (dirfd != AT_FDCWD) {
        printk("sys_openat: dirfd unimplemented\n");
        return -1;
    }

    OpContext ctx;
    bcache.begin_op(&ctx);
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, INODE_REGULAR, 0, 0, &ctx);
        if (ip == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
    } else {
        if ((ip = namei(path, &ctx)) == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
        inodes.lock(ip);
    }

    if ((f = file_alloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            file_close(f);
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

define_syscall(mkdirat, int dirfd, const char *path, int mode)
{
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mkdirat: dirfd unimplemented\n");
        return -1;
    }
    if (mode != 0) {
        printk("sys_mkdirat: mode unimplemented\n");
        return -1;
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DIRECTORY, 0, 0, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(mknodat, int dirfd, const char *path, mode_t mode, dev_t dev)
{
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mknodat: dirfd unimplemented\n");
        return -1;
    }

    unsigned int ma = major(dev);
    unsigned int mi = minor(dev);
    printk("mknodat: path '%s', major:minor %u:%u\n", path, ma, mi);
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DEVICE, (short)ma, (short)mi, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(chdir, const char *path)
{
    /**
     * (Final) TODO BEGIN 
     * 
     * Change the cwd (current working dictionary) of current process to 'path'.
     * You may need to do some validations.
     */
    // 1. 验证路径
    if (!user_strlen(path, 256))  // 确保路径的长度是合法的
        return -1;

    // 2. 获取目录的 Inode
    OpContext ctx;
    Inode *ip = NULL;
    bcache.begin_op(&ctx);

    // 查找路径对应的 Inode
    if ((ip = namei(path, &ctx)) == NULL) {
        bcache.end_op(&ctx);
        return -1;  // 路径无效，返回错误
    }

    // 3. 检查目标路径是否是目录
    if (ip->entry.type != INODE_DIRECTORY) {
        bcache.end_op(&ctx);
        return -1;  // 目标不是目录，返回错误
    }

    // 4. 设置进程的当前工作目录
    current->cwd = ip;  // 假设 `current` 是当前进程的指针，`cwd` 为进程的工作目录

    // 5. 清理
    bcache.end_op(&ctx);

    /* (Final) TODO END */
}

define_syscall(pipe2, int pipefd[2], int flags)
{

    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}