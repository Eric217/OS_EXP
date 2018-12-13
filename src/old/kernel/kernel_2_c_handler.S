[bits 32]
; 中断发生时，处理器要在目标栈中保存被中断进程的部分寄存器环境，自动完成。保存的寄存器名称及顺序是：
; (1）如果发生了特权级转移，如3->0，要把低特权级的ss及sp保存到栈中
; (2）eflags (3）压入返回地址 cs eip (4）如果此中断有错误码，压入错误码
%define ZERO         push 0; 若在相关的异常中cpu没有压入错误码,为了统一栈顶指针使add 4都能恢复，压0
%define ERROR_CODE   nop   ; 查表哪些中断需要操作

extern put_str             
extern idt_table           ; C 中断处理函数数组


section .data
 
global intr_entry_table
intr_entry_table:          ; 宏里数据段拼一起最后成为entry数组，表

%macro VECTOR 2
section .text
intr%1entry:               ; 每个中断向量号都对应一个中断处理程序 
   %2                      ; 压入eip后即执行
                           
   push ds                 ; 调用C程序前必须 backup reg
   push es
   push fs
   push gs
   pushad 

   ; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI。
   ; 8259A的操作控制字 OCW2，其中第5位是 EOI 位为1，其余位全为0，所以是0x20。
   ; 由于将来设置 8259A 为手动结束，中断处理程序中需要手动向 8259A 发送中断结束标记，
   ; 告知它当前中断处理程序已执行完成。否则 8259A 一直等待，不接受新的中断 
   mov al,  0x20           ; 中断结束命令EOI
   out 0xa0,al             ; 向从片发送
   out 0x20,al             ; 向主片发送

   push %1                 ; 压入向量号参数纯粹方便调试，索引 idt_name 打印
   call [idt_table + %1*4] ; C 

   jmp intr_exit
   
section .data
   dd intr%1entry	         ; 存储各个中断入口程序的地址，形成 intr_entry_table 数组
%endmacro

section .text
global intr_exit
intr_exit:
   add esp, 4              ; push %1
   popad
   pop gs
   pop fs
   pop es
   pop ds
   add esp, 4              ; ERROR_CODE
   iretd                   ; 从中断返回


; 下面定义了33个中断向量，0～19 为处理器内部固定的异常类型，20~31 是Intel保留。可用的中断向量号最低是32，将来会把IR0的中断向量号设置为32

; 第1个参数是中断向量号，用来表示：本宏是为了此中断向量号而定义的中断处理程序，或者说这是本宏实现的中断处理程序对应的中断向量
VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO 
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO 
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE 
VECTOR 0x0c,ZERO
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO 
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO 
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO 
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO 
VECTOR 0x20,ZERO
