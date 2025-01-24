#include <elf.h>                      // ELF文件格式相关的头文件
#include <common/string.h>            // 字符串操作相关的头文件
#include <common/defines.h>           // 定义常用宏和常量
#include <kernel/console.h>           // 内核控制台相关的头文件
#include <kernel/proc.h>              // 进程管理相关的头文件
#include <kernel/sched.h>             // 调度器相关的头文件
#include <kernel/syscall.h>           // 系统调用相关的头文件
#include <kernel/pt.h>                // 页表相关的头文件
#include <kernel/mem.h>               // 内存管理相关的头文件
#include <kernel/paging.h>            // 分页机制相关的头文件
#include <kernel/printk.h>            // 内核打印函数相关的头文件
#include <aarch64/trap.h>             // 异常处理相关的头文件
#include <fs/file.h>                  // 文件系统相关的头文件
#include <fs/inode.h>                 // inode相关的头文件

#define USER_STACK_TOP 0x800000000000 // 用户栈的顶部地址
#define USER_STACK_SIZE 0x800000      // 用户栈的大小（8MB）
#define RESERVE_SIZE 0x40             // 栈顶保留空间的大小
#define Error printk("\033[47;31m(Error)\033[0m") // 错误打印宏

extern int fdalloc(struct file *f);   // 分配文件描述符的外部函数声明

// 加载ELF文件头的函数
static int load_elf_header(Inode *ip, Elf64_Ehdr *elf) {
    // 从inode中读取ELF文件头到elf结构体中
    if (inodes.read(ip, (u8 *)elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        Error; // 如果读取失败，打印错误信息
        return -1; // 返回错误码
    }
    return 0; // 成功返回0
}

// 验证ELF文件头的函数
static int validate_elf_header(Elf64_Ehdr *elf) {
    u8 *e_ident = elf->e_ident; // 获取ELF文件的标识符
    // 检查ELF魔数（前4字节是否为ELF魔数）和文件类（是否为64位）
    if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 || e_ident[EI_CLASS] != ELFCLASS64) {
        Error; // 如果验证失败，打印错误信息
        return -1; // 返回错误码
    }
    return 0; // 成功返回0
}

// 加载程序头表的函数
static int load_program_headers(Inode *ip, Elf64_Ehdr *elf, struct pgdir *pgdir) {
    Elf64_Phdr phdr; // 程序头表项
    u64 section_top = 0; // 记录当前已加载的最高地址

    // 遍历所有程序头表项
    for (u64 i = 0, off = elf->e_phoff; i < elf->e_phnum; i++, off += sizeof(phdr)) {
        // 从inode中读取程序头表项到phdr结构体中
        if (inodes.read(ip, (u8 *)&phdr, off, sizeof(phdr)) != sizeof(phdr)) {
            Error; // 如果读取失败，打印错误信息
            return -1; // 返回错误码
        }
        if (phdr.p_type != PT_LOAD) continue; // 如果程序头表项不是LOAD类型，跳过

        // 更新已加载的最高地址
        section_top = MAX(section_top, phdr.p_vaddr + phdr.p_memsz);

        // 分配一个新的section结构体
        struct section *sec = (struct section *)kalloc(sizeof(struct section));
        init_section(sec); // 初始化section
        sec->begin = phdr.p_vaddr; // 设置section的起始地址

        // 根据程序头表项的flags字段判断是代码段还是数据段
        if (phdr.p_flags == (PF_R | PF_X)) {
            // 代码段（可读可执行）
            sec->flags = ST_TEXT; // 设置section类型为代码段
            sec->end = sec->begin + phdr.p_filesz; // 设置section的结束地址

            // 分配一个文件结构体，用于懒加载
            sec->fp = file_alloc();
            sec->fp->ip = inodes.share(ip); // 共享inode
            sec->fp->readable = TRUE; // 文件可读
            sec->fp->writable = FALSE; // 文件不可写
            sec->fp->ref = 1; // 引用计数为1
            sec->fp->off = 0; // 文件偏移量为0
            sec->fp->type = FD_INODE; // 文件类型为inode
            sec->length = phdr.p_filesz; // 文件长度
            sec->offset = phdr.p_offset; // 文件偏移量
        } else if (phdr.p_flags == (PF_R | PF_W)) {
            // 数据段（可读可写）
            sec->flags = ST_DATA; // 设置section类型为数据段
            sec->end = sec->begin + phdr.p_memsz; // 设置section的结束地址

            u64 filesz = phdr.p_filesz, offset = phdr.p_offset, va = phdr.p_vaddr;
            // 加载数据段的内容
            while (filesz) {
                u64 cursize = MIN(filesz, (u64)PAGE_SIZE - VA_OFFSET(va)); // 计算当前需要加载的大小
                void *p = kalloc_page(); // 分配一个物理页
                memset(p, 0, PAGE_SIZE); // 将物理页清零
                vmmap(pgdir, PAGE_BASE(va), p, PTE_USER_DATA | PTE_RW); // 将物理页映射到虚拟地址
                // 从inode中读取数据到物理页
                if (inodes.read(ip, (u8 *)(p + VA_OFFSET(va)), offset, cursize) != cursize) {
                    Error; // 如果读取失败，打印错误信息
                    return -1; // 返回错误码
                }
                filesz -= cursize; // 更新剩余的文件大小
                offset += cursize; // 更新文件偏移量
                va += cursize; // 更新虚拟地址
            }

            // 处理BSS段（未初始化数据段）
            if (PAGE_BASE(va) + PAGE_SIZE < phdr.p_vaddr + phdr.p_memsz) {
                va = PAGE_BASE(va) + PAGE_SIZE; // 更新虚拟地址
                filesz = phdr.p_vaddr + phdr.p_memsz - va; // 计算BSS段的大小
                // 将BSS段映射到零页（初始化为0）
                while (filesz > 0) {
                    u64 cursize = MIN((u64)PAGE_SIZE, filesz); // 计算当前需要映射的大小
                    vmmap(pgdir, PAGE_BASE(va), get_zero_page(), PTE_USER_DATA | PTE_RO); // 映射零页
                    filesz -= cursize; // 更新剩余的BSS段大小
                    va += cursize; // 更新虚拟地址
                }
            }
        } else {
            Error; // 如果程序头表项的flags字段不合法，打印错误信息
            return -1; // 返回错误码
        }

        // 将section插入到页表的section链表中
        _insert_into_list(&pgdir->section_head, &sec->stnode);
    }

    return 0; // 成功返回0
}

// 设置用户栈的函数
static int setup_user_stack(struct pgdir *pgdir, char *const argv[], char *const envp[]) {
    u64 top = USER_STACK_TOP - RESERVE_SIZE; // 计算栈顶地址
    struct section *st_ustack = (struct section *)kalloc(sizeof(struct section)); // 分配用户栈的section
    memset(st_ustack, 0, sizeof(struct section)); // 清零section
    st_ustack->begin = USER_STACK_TOP - USER_STACK_SIZE; // 设置用户栈的起始地址
    st_ustack->end = USER_STACK_TOP; // 设置用户栈的结束地址
    st_ustack->flags = ST_USER_STACK; // 设置section类型为用户栈
    init_list_node(&st_ustack->stnode); // 初始化链表节点
    _insert_into_list(&pgdir->section_head, &st_ustack->stnode); // 将用户栈插入到页表的section链表中

    u64 argc = 0, arg_len = 0, envc = 0, env_len = 0, zero = 0;
    // 计算环境变量的总长度
    if (envp) {
        while (envp[envc]) {
            env_len += strlen(envp[envc]) + 1; // 累加每个环境变量的长度
            envc++; // 环境变量计数
        }
    }
    // 计算命令行参数的总长度
    if (argv) {
        while (argv[argc]) {
            arg_len += strlen(argv[argc]) + 1; // 累加每个命令行参数的长度
            argc++; // 命令行参数计数
        }
    }

    u64 str_total_len = env_len + arg_len; // 计算所有字符串的总长度
    u64 str_start = top - str_total_len; // 计算字符串的起始地址
    u64 ptr_tot = (2 + argc + envc + 1) * 8; // 计算指针数组的总长度
    u64 argc_start = (str_start - ptr_tot) & (~0xf); // 计算栈顶地址（16字节对齐）
    if (argc_start < USER_STACK_TOP - USER_STACK_SIZE) {
        PANIC(); // 如果栈顶地址超出用户栈范围，触发内核恐慌
    }
    u64 argv_start = argc_start + 8; // 计算argv数组的起始地址
    u64 sp = argc_start; // 设置栈指针
    copyout(pgdir, (void *)sp, &argc, 8); // 将argc写入栈中

    // 将命令行参数写入栈中
    for (u64 i = 0; i < argc; i++) {
        usize len = strlen(argv[i]) + 1; // 计算每个命令行参数的长度
        copyout(pgdir, (void *)str_start, argv[i], len); // 将命令行参数写入栈中
        copyout(pgdir, (void *)argv_start, &str_start, 8); // 将命令行参数的地址写入argv数组
        str_start += len; // 更新字符串的起始地址
        argv_start += 8; // 更新argv数组的指针
    }
    copyout(pgdir, (void *)argv_start, &zero, 8); // 在argv数组末尾写入0

    // 将环境变量写入栈中
    argv_start += 8;
    for (u64 i = 0; i < envc; i++) {
        usize len = strlen(envp[i]) + 1; // 计算每个环境变量的长度
        copyout(pgdir, (void *)str_start, envp[i], len); // 将环境变量写入栈中
        copyout(pgdir, (void *)argv_start, &str_start, 8); // 将环境变量的地址写入envp数组
        str_start += len; // 更新字符串的起始地址
        argv_start += 8; // 更新envp数组的指针
    }
    copyout(pgdir, (void *)argv_start, &zero, 8); // 在envp数组末尾写入0

    thisproc()->ucontext->sp = sp; // 设置用户栈指针
    return 0; // 成功返回0
}

// execve系统调用的实现
int execve(const char *path, char *const argv[], char *const envp[]) {
    Elf64_Ehdr elf; // ELF文件头
    Inode *ip; // 文件inode
    struct pgdir *pgdir, *oldpgdir; // 新页表和旧页表
    Proc *curproc = thisproc(); // 当前进程

    OpContext ctx;
    bcache.begin_op(&ctx); // 开始文件系统操作

    // 根据路径查找inode
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx); // 结束文件系统操作
        Error; // 如果找不到文件，打印错误信息
        return -1; // 返回错误码
    }

    inodes.lock(ip); // 锁定inode
    pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir)); // 分配新页表
    init_pgdir(pgdir); // 初始化新页表

    // 加载ELF文件头、验证ELF文件头、加载程序头表
    if (load_elf_header(ip, &elf) < 0 || validate_elf_header(&elf) < 0 || load_program_headers(ip, &elf, pgdir) < 0) {
        goto bad; // 如果失败，跳转到错误处理
    }

    inodes.unlockput(&ctx, ip); // 解锁并释放inode
    bcache.end_op(&ctx); // 结束文件系统操作

    // 初始化堆段
    struct section *heap = (struct section *)kalloc(sizeof(struct section));
    memset(heap, 0, sizeof(struct section)); // 清零堆段
    heap->begin = heap->end = PAGE_BASE(elf.e_entry) + PAGE_SIZE; // 设置堆段的起始和结束地址
    heap->flags = ST_HEAP; // 设置section类型为堆段
    _insert_into_list(&pgdir->section_head, &heap->stnode); // 将堆段插入到页表的section链表中

    // 设置用户栈
    if (setup_user_stack(pgdir, argv, envp) < 0) {
        goto bad; // 如果失败，跳转到错误处理
    }

    // 切换到新页表
    oldpgdir = &curproc->pgdir; // 保存旧页表
    free_pgdir(oldpgdir); // 释放旧页表
    curproc->ucontext->elr = elf.e_entry; // 设置程序入口地址
    memcpy(&curproc->pgdir, pgdir, sizeof(struct pgdir)); // 复制新页表到当前进程
    init_list_node(&curproc->pgdir.section_head); // 初始化新页表的section链表
    _insert_into_list(&pgdir->section_head, &curproc->pgdir.section_head); // 将新页表插入到section链表中
    _detach_from_list(&pgdir->section_head); // 从链表中分离新页表
    kfree(pgdir); // 释放新页表的内存
    attach_pgdir(&curproc->pgdir); // 切换到新页表
    return 0; // 成功返回0

bad:
    // 错误处理
    if (pgdir) {
        free_pgdir(pgdir); // 释放新页表
    }
    if (ip) {
        inodes.unlockput(&ctx, ip); // 解锁并释放inode
        bcache.end_op(&ctx); // 结束文件系统操作
    }
    return -1; // 返回错误码
}