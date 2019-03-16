#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"

/*负责初始化所有模块 */
void init_all() {
	put_str("Initializing...\n");
   	idt_init();   	// 初始化中断
   	timer_init();	// 初始化PIT
   	thread_init(); 
   	mem_init();
   	console_init();
   	keyboard_init();
    tss_init();




   	intr_enable();	 
}
