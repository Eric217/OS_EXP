#include "print.h"
#include "init.h"
#include "interrupt.h"
#include "thread.h"
#include "console.h"

void my_thread_1(void *);
void my_thread_2(void *);

int main(void) { 
   init_all();
 
   thread_start("thread_1", 30, my_thread_1, "t1_arg ");
   thread_start("thread_2", 10, my_thread_2, "t2_arg ");

   intr_enable();	// 打开中断,使时钟中断起作用
   const char * main_str = "Main ";
   while(1)
      console_put_str(main_str);
   return 0;
} 

void my_thread_1(void * arg) {
	char *str = (char *)arg;
	while(1) 
		console_put_str(str);
}

void my_thread_2(void * arg) {
	char *str = (char *)arg;
	while(1) 
		console_put_str(str);
}
 