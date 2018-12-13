#include "print.h"
#include "init.h"
void main(void) {
   put_str("I am kernel\n"); 
   put_int(17);
   put_char('\n');
   put_int(0x00021a3f);
   put_char('\n'); 
   // 测试 print

   init_all();
   asm volatile("sti");	     // 为演示中断处理, 在此临时开中断
 
   while(1);
} 