#include "debug.h"
#include "print.h"
#include "interrupt.h"

#define println(str) put_str(str); put_char('\n');

/* 打印文件名,行号,函数名,条件 并使程序悬停 */
void panic_spin(char* filename, 
                int line,
                const char* func,  
                const char* condition) 
{
    intr_disable();    // 因为有时候会单独调用panic_spin,所以在此处关中断。
    put_str("\n!!!!! error !!!!!\n");
    put_str("filename:");println(filename);
    put_str("line:0x");put_int(line);put_char('\n');
    put_str("function:");println((char*)func); 
    put_str("condition:");println((char*)condition); 
    while(1);
}

