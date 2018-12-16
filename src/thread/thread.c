#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"

#define PG_SIZE 4096

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
   function(func_arg); 
}

/* 初始化线程栈thread_stack,将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
   // 预留中断使用的栈空间 位于 kernel.S 的中断代码会通过此栈保存上下文
   pthread->self_kstack -= sizeof(struct intr_stack); 
   // 留出线程栈的内存 然后把此区域强转为线程栈（牛批啊！！！，第一次感觉到灵活与强大）
   pthread->self_kstack -= sizeof(struct thread_stack);
   struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
   kthread_stack->eip = kernel_thread;
   kthread_stack->function = function;
   kthread_stack->func_arg = func_arg;
   kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

// 初始化线程基本信息 
void init_thread(struct task_struct* pthread, char* name, int prio) {
   memset(pthread, 0, sizeof(*pthread));
   strcpy(pthread->name, name);
   pthread->status = TASK_RUNNING; 
   pthread->priority = prio;
   /* self_kstack 是线程在内核态下自己的栈顶地址，也就是4KB最高处 */
   pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
   pthread->stack_magic = 0x19870916;	  // 自定义的魔数
}

// 创建线程 优先级：prio 名：name 线程执行的函数、参数：function(func_arg) 
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
   // 用户、内核进程的 pcb 都位于内核空间，一个PCB占4KB
   struct task_struct* thread = get_kernel_pages(1);

   init_thread(thread, name, prio);
   thread_create(thread, function, func_arg);
   // pop esp升高，直到 eip函数指针，ret，正好压入 eip 执行！！！！
   asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" : : "g" (thread->self_kstack) : "memory");
   return thread;
}
