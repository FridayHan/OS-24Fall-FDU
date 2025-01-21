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
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <fcntl.h>

extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */

    // 使用 file_alloc 分配文件结构
    struct file *f = file_alloc();
    if (!f) {
        printk("execve: cannot allocate file\n");
        return -1;
    }

    // 打开文件

    // 读取 ELF 头
    Elf64_Ehdr elf;
    if (file_read(f, (char *)&elf, sizeof(elf)) != sizeof(elf)) {
        printk("execve: read elf header failed\n");
        file_close(f);
        return -1;
    }

    if (elf.e_ident[EI_MAG0] != 0x7F || elf.e_ident[EI_MAG1] != 'E' || elf.e_ident[EI_MAG2] != 'L' || elf.e_ident[EI_MAG3] != 'F') {
        printk("execve: not an elf file\n");
        file_close(f);
        return -1;
    }

    // 分配新的页目录
    struct pgdir new_pgdir;
    init_pgdir(&new_pgdir);

    // 加载程序段
    for (int i = 0; i < elf.e_phnum; i++) {
        Elf64_Phdr ph;

        // 使用 file_lseek 和 file_read 读取程序头
        f->off = elf.e_phoff + i * sizeof(ph);
        if (file_read(f, (char *)&ph, sizeof(ph)) != sizeof(ph)) {
            printk("execve: read program header failed\n");
            file_close(f);
            free_pgdir(&new_pgdir);
            return -1;
        }

        if (ph.p_type != PT_LOAD) {
            continue;
        }

        u64 va = ph.p_vaddr;
        u64 sz = ph.p_memsz;
        u64 off = ph.p_offset;

        // 分配内存
        for (u64 addr = (va & ~(PAGE_SIZE - 1)); addr < va + sz; addr += PAGE_SIZE) {
            char *pa = kalloc_page();
            if (!pa) {
                printk("execve: kalloc_page failed\n");
                file_close(f);
                free_pgdir(&new_pgdir);
                return -1;
            }
            vmmap(&new_pgdir, addr, pa, PTE_VALID | PTE_RW);
        }

        // 读取文件内容到内存

        // 使用 file_lseek 和 file_read 读取文件内容
        f->off = off;
        if (file_read(f, (char *)(va & ~(PAGE_SIZE - 1)), (isize)sz) != (isize)sz) {
            printk("execve: read file content failed\n");
            file_close(f);
            free_pgdir(&new_pgdir);
            return -1;
        }
    }

    // 设置用户上下文
    Proc *p = thisproc();
    free_pgdir(&p->pgdir);
    p->pgdir = new_pgdir;
    attach_pgdir(&p->pgdir);

    p->ucontext->elr = elf.e_entry;
    p->ucontext->sp = elf.e_entry + 0x100000; // 设置栈指针

    file_close(f);
    return 0;
    /* (Final) TODO END */
}