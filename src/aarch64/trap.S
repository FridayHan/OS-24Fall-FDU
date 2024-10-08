#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
// TODO: save UserContext

sub sp, sp, #0x110           // 31 个寄存器 + sp + pc + pstate = 0x110 字节
stp x0, x1, [sp, #0x00]
stp x2, x3, [sp, #0x10]
stp x4, x5, [sp, #0x20]
stp x6, x7, [sp, #0x30]
stp x8, x9, [sp, #0x40]
stp x10, x11, [sp, #0x50]
stp x12, x13, [sp, #0x60]
stp x14, x15, [sp, #0x70]
stp x16, x17, [sp, #0x80]
stp x18, x19, [sp, #0x90]
stp x20, x21, [sp, #0xA0]
stp x22, x23, [sp, #0xB0]
stp x24, x25, [sp, #0xC0]
stp x26, x27, [sp, #0xD0]
stp x28, x29, [sp, #0xE0]
stp lr, x30, [sp, #0xF0]

// 保存用户栈指针、程序计数器和处理器状态寄存器
mrs x0, sp_el0               // 获取用户态的 SP
str x0, [sp, #0x100]         // 保存用户态的 sp
mrs x0, elr_el1              // 获取用户态的 PC
str x0, [sp, #0x108]         // 保存用户态的 pc
mrs x0, spsr_el1             // 获取用户态的处理器状态寄存器
str x0, [sp, #0x110]         // 保存用户态的 pstate

bl trap_global_handler

.global trap_return
trap_return:
// TODO: restore UserContext

// 恢复用户栈指针、程序计数器和处理器状态寄存器
ldr x0, [sp, #0x100]         // 恢复用户态的 sp
msr sp_el0, x0               // 恢复到 sp_el0
ldr x0, [sp, #0x108]         // 恢复用户态的 pc
msr elr_el1, x0              // 恢复到 elr_el1
ldr x0, [sp, #0x110]         // 恢复用户态的 pstate
msr spsr_el1, x0             // 恢复到 spsr_el1

// 恢复通用寄存器
ldp x0, x1, [sp, #0x00]      // 恢复 x0-x1
ldp x2, x3, [sp, #0x10]      // 恢复 x2-x3
ldp x4, x5, [sp, #0x20]      // 恢复 x4-x5
ldp x6, x7, [sp, #0x30]      // 恢复 x6-x7
ldp x8, x9, [sp, #0x40]      // 恢复 x8-x9
ldp x10, x11, [sp, #0x50]    // 恢复 x10-x11
ldp x12, x13, [sp, #0x60]    // 恢复 x12-x13
ldp x14, x15, [sp, #0x70]    // 恢复 x14-x15
ldp x16, x17, [sp, #0x80]    // 恢复 x16-x17
ldp x18, x19, [sp, #0x90]    // 恢复 x18-x19
ldp x20, x21, [sp, #0xA0]    // 恢复 x20-x21
ldp x22, x23, [sp, #0xB0]    // 恢复 x22-x23
ldp x24, x25, [sp, #0xC0]    // 恢复 x24-x25
ldp x26, x27, [sp, #0xD0]    // 恢复 x26-x27
ldp x28, x29, [sp, #0xE0]    // 恢复 x28 和帧指针 (x29)
ldp lr, x30, [sp, #0xF0]     // 恢复链接寄存器 (lr) 和返回地址 (x30)
add sp, sp, #0x110           // 恢复栈指针

eret
