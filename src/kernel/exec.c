#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define USER_STACK_TOP 0x800000000000 // 用户栈的顶部地址
#define USER_STACK_SIZE 0x800000      // 用户栈的大小（8MB）
#define RESERVE_SIZE 0x40             // 栈顶保留空间的大小

extern int fdalloc(struct file *f);

static int load_elf_header(Inode *ip, Elf64_Ehdr *elf)
{
    if (inodes.read(ip, (u8 *)elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) return -1;
    return 0;
}

static int check_elf_header(Elf64_Ehdr *elf)
{
    u8 *e_ident = elf->e_ident;
    if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 || e_ident[EI_CLASS] != ELFCLASS64) return -1;
    return 0;
}

static int load_and_map_elf_segments(Inode *ip, Elf64_Ehdr *elf, struct pgdir *pgdir)
{
    Elf64_Phdr phdr;
    u64 section_stack_top = 0;

    for (u64 i = 0, off = elf->e_phoff; i < elf->e_phnum; i++, off += sizeof(Elf64_Phdr))
    {
        if (inodes.read(ip, (u8 *)&phdr, off, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) return -1;
        if (phdr.p_type != PT_LOAD) continue;

        section_stack_top = MAX(section_stack_top, phdr.p_vaddr + phdr.p_memsz);

        struct section *sec = (struct section *)kalloc(sizeof(struct section));
        memset(sec, 0, sizeof(struct section));
        init_sections(&sec->stnode);
        sec->begin = phdr.p_vaddr;

        if (phdr.p_flags == (PF_R | PF_X))
        {
            // text segment (RX)
            sec->flags = ST_TEXT;
            sec->end = sec->begin + phdr.p_filesz;

            sec->fp = file_alloc();
            sec->fp->ip = inodes.share(ip);
            sec->fp->readable = TRUE;
            sec->fp->writable = FALSE;
            sec->fp->ref = 1;
            sec->fp->off = 0;
            sec->fp->type = FD_INODE;
            sec->length = phdr.p_filesz;
            sec->offset = phdr.p_offset;
        }
        else if (phdr.p_flags == (PF_R | PF_W))
        {
            // data segment (RW)
            sec->flags = ST_DATA;
            sec->end = sec->begin + phdr.p_memsz;

            u64 filesz = phdr.p_filesz, offset = phdr.p_offset, va = phdr.p_vaddr;
            while (filesz)
            {
                u64 cursize = MIN(filesz, (u64)PAGE_SIZE - VA_OFFSET(va));
                void *pg = kalloc_page();
                memset(pg, 0, PAGE_SIZE);
                vmmap(pgdir, PAGE_BASE(va), pg, PTE_USER_DATA | PTE_RW);
                if (inodes.read(ip, (u8 *)(pg + VA_OFFSET(va)), offset, cursize) != cursize) return -1;
                filesz -= cursize;
                offset += cursize;
                va += cursize;
            }

            // BSS (If located in the current page, it's already zeroed)
            if (PAGE_BASE(va) + PAGE_SIZE < phdr.p_vaddr + phdr.p_memsz)
            {
                va = PAGE_BASE(va) + PAGE_SIZE;
                filesz = phdr.p_vaddr + phdr.p_memsz - va;
                while (filesz > 0)
                {
                    u64 cursize = MIN((u64)PAGE_SIZE, filesz);
                    vmmap(pgdir, PAGE_BASE(va), get_zero_page(), PTE_USER_DATA | PTE_RO);
                    filesz -= cursize;
                    va += cursize;
                }
            }
        }
        else
        {
            printk("Invalid program header flags\n");
            return -1;
        }

        _insert_into_list(&pgdir->section_head, &sec->stnode);
    }

    return 0;
}

static int setup_user_stack(struct pgdir *pgdir, char *const argv[], char *const envp[])
{
    u64 stack_top = USER_STACK_TOP - RESERVE_SIZE;
    struct section *st_ustack = (struct section *)kalloc(sizeof(struct section));
    memset(st_ustack, 0, sizeof(struct section));
    init_sections(&st_ustack->stnode);
    st_ustack->begin = stack_top - USER_STACK_SIZE;
    st_ustack->end = stack_top;
    st_ustack->flags = ST_USTACK;
    _insert_into_list(&pgdir->section_head, &st_ustack->stnode);

    u64 argc = 0, arg_len = 0, envc = 0, env_len = 0, zero = 0;
    if (envp)
    {
        while (envp[envc])
        {
            env_len += strlen(envp[envc]) + 1;
            envc++;
        }
    }

    if (argv)
    {
        while (argv[argc])
        {
            arg_len += strlen(argv[argc]) + 1;
            argc++;
        }
    }

    u64 str_total_len = env_len + arg_len;
    u64 str_start = stack_top - str_total_len;
    u64 ptr_tot = (2 + argc + envc + 1) * 8;
    u64 argc_start = (str_start - ptr_tot) & (~0xf);
    if (argc_start < stack_top - USER_STACK_SIZE)
        PANIC();
    u64 argv_start = argc_start + 8;
    u64 sp = argc_start;
    copyout(pgdir, (void *)sp, &argc, 8);

    for (u64 i = 0; i < argc; i++)
    {
        usize len = strlen(argv[i]) + 1;
        copyout(pgdir, (void *)str_start, argv[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8);

    argv_start += 8;
    for (u64 i = 0; i < envc; i++) {
        usize len = strlen(envp[i]) + 1;
        copyout(pgdir, (void *)str_start, envp[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8);
    thisproc()->ucontext->sp = sp;
    return 0;
}

// int execve(const char *path, char *const argv[], char *const envp[])
// {
//     /* (Final) TODO BEGIN */
//     printk("execve: path=%s\n", path);
//     Elf64_Ehdr elf;
//     Elf64_Phdr phdr;
//     Inode* ip;
//     struct pgdir *pgdir, *oldpgdir;
//     Proc *p = thisproc();

//     /*
//      * Step1: Load data from the file stored in `path`.
//      * The first `sizeof(struct Elf64_Ehdr)` bytes is the ELF header part.
//      * You should check the ELF magic number and get the `e_phoff` and `e_phnum` which is the starting byte of program header.
//      */
//     OpContext ctx;
//     bcache.begin_op(&ctx);

//     if (ip = namei(path, &ctx), !ip) {
//         bcache.end_op(&ctx);
//         printk("execve: file %s not found\n", path);
//         return -1;
//     }

//     inodes.lock(ip);
//     pgdir = (struct pgdir *)kalloc(sizeof(pgdir));
//     init_pgdir(pgdir);

//     if (load_elf_header(ip, &elf) < 0 || check_elf_header(&elf) < 0 || load_and_map_elf_segments(ip, &elf, pgdir) < 0) {
//         return -1;
//     }

//     if (inodes.read(ip, (char*)&elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
//         bcache.end_op(&ctx);
//         printk("execve: file %s is not a valid ELF file\n", path);
//         return -1;
//     }



//     /* Step2: Load program headers and the program itself
//      * Program headers are stored like: struct Elf64_Phdr phdr[e_phnum];
//      * e_phoff is the offset of the headers in file, namely, the address of phdr[0].
//      * For each program header, if the type(p_type) is LOAD, you should load them:
//      * A naive way is 
//      * (1) allocate memory, va region [vaddr, vaddr+filesz)
//      * (2) copy [offset, offset + filesz) of file to va [vaddr, vaddr+filesz) of memory
//      * Since we have applied dynamic virtual memory management, you can try to only set the file and offset (lazy allocation)
//      * (hints: there are two loadable program headers in most exectuable file at this lab, the first header indicates the text section(flag=RX) and the second one is the data+bss section(flag=RW). You can verify that by check the header flags. The second header has [p_vaddr, p_vaddr+p_filesz) the data section and [p_vaddr+p_filesz, p_vaddr+p_memsz) the bss section which is required to set to 0, you may have to put data and bss in a single struct section. COW by using the zero page is encouraged)
//      */


//     /* Step3: Allocate and initialize user stack.
//      * The va of the user stack is not required to be any fixed value. It can be randomized. (hints: you can directly allocate user stack at one time, or apply lazy allocation)
//      * Push argument strings.
//      * The initial stack may like
//      *   +-------------+
//      *   | envp[m] = 0 |
//      *   +-------------+
//      *   |    ....     |
//      *   +-------------+
//      *   |   envp[0]   |  ignore the envp if you do not want to implement
//      *   +-------------+
//      *   | argv[n] = 0 |  n == argc
//      *   +-------------+
//      *   |    ....     |
//      *   +-------------+
//      *   |   argv[0]   |
//      *   +-------------+
//      *   |    argc     |
//      *   +-------------+  <== sp
//      * 
//      * ## Example
//      * sp -= 8; *(size_t *)sp = argc; (hints: sp can be directly written if current pgdir is the new one)
//      * thisproc()->tf->sp = sp; (hints: Stack pointer must be aligned to 16B!)
//      * The entry point addresses is stored in elf_header.entry
//      */
//     /* (Final) TODO END */
// }

int execve(const char *path, char *const argv[], char *const envp[])
{
    Elf64_Ehdr elf;
    Inode *ip;
    struct pgdir *pgdir, *oldpgdir;
    Proc *p = thisproc();

    OpContext ctx;
    bcache.begin_op(&ctx);

    /* Step 1: Load data from the file stored in `path`. */
    printk("execve: path=%s\n", path);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(ip);
    pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir));
    init_pgdir(pgdir);

    /* Step 2: Load program headers and the program itself. */
    // 加载ELF文件头、验证ELF文件头、加载程序头表
    if (load_elf_header(ip, &elf) < 0 || check_elf_header(&elf) < 0 || load_and_map_elf_segments(ip, &elf, pgdir) < 0) {
        // 释放资源并返回错误
        free_pgdir(pgdir); // 释放新页表
        inodes.unlock(ip); // 解锁并释放inode
        bcache.end_op(&ctx); // 结束文件系统操作
        return -1;
    }

    inodes.unlock(ip); // 解锁并释放inode
    bcache.end_op(&ctx); // 结束文件系统操作

    /* Step 3: Allocate and initialize user stack. */
    // 初始化堆段
    struct section *heap = (struct section *)kalloc(sizeof(struct section));
    memset(heap, 0, sizeof(struct section)); // 清零堆段
    heap->begin = heap->end = PAGE_BASE(elf.e_entry) + PAGE_SIZE; // 设置堆段的起始和结束地址
    heap->flags = ST_HEAP; // 设置section类型为堆段
    _insert_into_list(&pgdir->section_head, &heap->stnode); // 将堆段插入到页表的section链表中

    // 设置用户栈
    if (setup_user_stack(pgdir, argv, envp) < 0) {
        // 如果设置栈失败，释放资源并返回错误
        free_pgdir(pgdir); // 释放新页表
        return -1;
    }

    /* Step 4: Switch to the new page table. */
    // 切换到新页表
    oldpgdir = &p->pgdir; // 保存旧页表
    free_pgdir(oldpgdir); // 释放旧页表
    p->ucontext->elr = elf.e_entry; // 设置程序入口地址
    memcpy(&p->pgdir, pgdir, sizeof(struct pgdir)); // 复制新页表到当前进程
    init_list_node(&p->pgdir.section_head); // 初始化新页表的section链表
    _insert_into_list(&pgdir->section_head, &p->pgdir.section_head); // 将新页表插入到section链表中
    _detach_from_list(&pgdir->section_head); // 从链表中分离新页表
    kfree(pgdir); // 释放新页表的内存
    attach_pgdir(&p->pgdir); // 切换到新页表
    return 0;
    /* (Final) TODO END */
}