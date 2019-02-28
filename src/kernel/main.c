#include "print.h"
#include "init.h"
#include "interrupt.h"
#include "thread.h"

void my_thread_1(void *);
void my_thread_2(void *);

int main(void) { 
   init_all();
 
   thread_start("thread_1", 30, my_thread_1, "t1_arg ");
   // thread_start("thread_2", 10, my_thread_2, "t2_arg ");

   intr_enable();	// 打开中断,使时钟中断起作用
   while(1)
      put_str("Main ");
   return 0;
} 

void my_thread_1(void * arg) {
	char *str = (char *)arg;
	while(1) 
		println(str);
}

void my_thread_2(void * arg) {
	char *str = (char *)arg;
	while(1) 
		println(str);
}
 