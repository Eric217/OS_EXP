#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"

/*负责初始化所有模块 */
void init_all() {
   put_str("Initializing...\n");
   idt_init();   	// 初始化中断
   timer_init();	// 初始化PIT
   thread_init(); // 初始化线程相关结构
   mem_init();
   console_init();
}
