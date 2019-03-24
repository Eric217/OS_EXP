#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL 0x20         // 这里用的可编程中断控制器是8259A,主片的控制端口是0x20
#define PIC_M_DATA 0x21         // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0         // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1         // 从片的数据端口是0xa1

#define IDT_DESC_CNT 0x81       // 目前总共支持的中断数 33个

#define EFLAGS_IF 0x00000200    // eflags寄存器中的if位为1的16进制表示
// 寄存器约束g 约束 EFLAG_VAR 可以放在内存或寄存器中，pushfl 把 eflags 压栈，
// popl 将其弹出到与 EFLAG_VAR 关联的约束中，C变量 EFLAG_VAR 得到 eflags 值。
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0" : "=g" (EFLAG_VAR))

// 静态函数声明,全局数据结构
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);

static struct gate_desc idt[IDT_DESC_CNT];
// idt是中断描述符表,本质上就是个中断门描述符数组
// 注意区分 中断描述符数组，中断程序入口数组，中断处理程序数组
// 一会要用 中段处理程序入口数组，来初始化（填充进）中断描述符中。入口程序里，会call 中断处理程序
// 真正的中断处理程序，初始化时都是相同的处理方式：打印
// 因为大部分中断是未处理的异常引起的，所以中断程序初始化就像是异常处理函数初始化，
// 对于有意的中断，我们叫做 注册中断处理程序，即替代 处理程序数组里存的那个指针
// lidt 之后，中断控制器收到中断号，发给CPU作为索引 查描述符表，找到中断处理程序入口，执行完退出
// 那么为什么要拐弯抹角中间插一个入口表呢，直接让 idt 存中断程序不行吗？我个人的理解，
// 中断程序是C写的，编译后寄存器根本控制不住，必须在执行C前备份reg，执行完C再恢复，还有中断返回、控制器操作等等，
// 尽管这些可以在C里内联写，但每个程序都要重复这段，因此单独一个 kernel.S 做一个入口表

extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 指向kernel.S中的中断处理函数数组
extern uint32_t syscall_handler(void);

intr_handler idt_table[IDT_DESC_CNT];               // 映射上面的数组

char* intr_name[IDT_DESC_CNT];                      // 用于保存异常的名字，方便根据 vec_num DEBUG

// 通用中断处理函数，用于异常出现时处理
static void general_intr_handler(uint8_t vec_num) {
    if (vec_num == 0x27 || vec_num == 0x2f) {
        // IRQ7 IRQ15 会产生伪中断（spurious interrupt）无需处理
        // 0x2f 是从片 8259A 上的最后一个 IRQ 引脚，保留项
        return;
    }
    /* 将光标置为0,从屏幕左上角清出一片打印异常信息的区域,方便阅读 */
    set_cursor(0);
    int cursor_pos = 0;
    while(cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }
    
    set_cursor(0);  // 重置光标为屏幕左上角
    put_str("!!!!!!!      excetion message begin  !!!!!!!!\n");
    set_cursor(88);  // 从第2行第8个字符开始打印
    put_str(intr_name[vec_num]);
    if (vec_num == 14) {    // 若为Pagefault,将缺失的地址打印出来并悬停
        int page_fault_vaddr = 0;
        asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));   // cr2是存放造成page_fault的地址
        put_str("\npage fault addr is ");put_int(page_fault_vaddr);
    }
    put_str("\n!!!!!!!      excetion message end    !!!!!!!!\n");
    // 能进入中断处理程序就表示已经处在关中断情况下,
    // 不会出现调度进程的情况。故下面的死循环不会再被中断。
    while(1);
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void) {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        // kernel.S call [idt_table + vec_num*4]
        // 先统一初始化为 general，将来某些外设需要重写中断处理程序
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown"; // 先统一赋值 unknown
    }
    intr_name[0]  = "#DE Divide Error";
    intr_name[1]  = "#DB Debug Exception";
    intr_name[2]  = "NMI Interrupt";
    intr_name[3]  = "#BP Breakpoint Exception";
    intr_name[4]  = "#OF Overflow Exception";
    intr_name[5]  = "#BR BOUND Range Exceeded Exception";
    intr_name[6]  = "#UD Invalid Opcode Exception";
    intr_name[7]  = "#NM Device Not Available Exception";
    intr_name[8]  = "#DF Double Fault Exception";
    intr_name[9]  = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] intel 保留项
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 初始化可编程中断控制器8259A */
static void pic_init(void) {
    
    /* 初始化主片 详见331/342 */
    outb (PIC_M_CTRL, 0x11);   // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb (PIC_M_DATA, 0x20);   // ICW2: 起始中断向量号为0x20,也就是IR[0-7] 为 0x20 ~ 0x27.
    outb (PIC_M_DATA, 0x04);   // ICW3: IR2接从片.
    outb (PIC_M_DATA, 0x01);   // ICW4: 8086模式, 正常EOI
    
    /* 初始化从片 */
    outb (PIC_S_CTRL, 0x11);    // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb (PIC_S_DATA, 0x28);    // ICW2: 起始中断向量号为0x28,也就是IR[8-15] 为 0x28 ~ 0x2F.
    outb (PIC_S_DATA, 0x02);    // ICW3: 设置从片连接到主片的IR2引脚
    outb (PIC_S_DATA, 0x01);    // ICW4: 8086模式, 正常EOI（手动结束）
    
    put_str("     pic_init done!\n");
    
    // 目前 接受时钟、键盘 中断
    outb (PIC_M_DATA, 0xfc);    // 对应位写1屏蔽中断
    outb (PIC_S_DATA, 0xff);    // 从片屏蔽所有中断
}

/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function) {
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void idt_desc_init(void) {

    for (int i = 0; i < IDT_DESC_CNT - 1; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    // syscall 中断门 DPL3
    make_idt_desc(&idt[IDT_DESC_CNT-1], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("     idt_desc_init done!\n");
}


/* 开中断并返回开中断前的状态*/
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti");   // 开中断,sti指令将IF位置1
        return old_status;
    }
}

/* 关中断,并且返回关中断前的状态 */
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli" : : : "memory"); // 关中断,cli指令将IF位置0
        return old_status;
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}

/* 将中断状态设置为status，返回旧status */
enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF; // 取 IF 位
}

/* 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function) {
    /* idt_table数组中的函数是在进入中断后根据中断向量号调用的,
     * 见kernel/kernel.S的call [idt_table + %1*4] */
    idt_table[vector_no] = function;
}

/*完成有关中断的所有初始化工作*/
void idt_init() {
    put_str("   idt_init start...\n");
    idt_desc_init();       // 初始化中断描述符表
    exception_init();     // 异常名初始化并注册通常的中断处理函数
    pic_init();           // 初始化8259A
    
    /* 加载idt: limit or int-pointer-shl-16 -> uint64, lidt ptr-of-48-bit */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m" (idt_operand));
    put_str("   idt_init done!\n");
}

