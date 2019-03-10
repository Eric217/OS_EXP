#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"

/* 自定义通用函数类型,它将在很多线程函数中做为形参类型 */
typedef void thread_func(void*);

/* 进程或线程的状态 */
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

/***********   中断栈intr_stack   ***********
 * 中断发生时，详见 kernel.S 为保护本程序(线程或进程)的上下文，
 * 压入一系列 reg。按顺序，即为本结构。 
 ********************************************/
struct intr_stack {
    uint32_t vec_no;    // push %1 压入的中断号

    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // 虽然 esp 也压入，但esp是不断变化的 
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    
    uint32_t err_code;  // err_code 

    void (*eip) (void); // old eip
    uint32_t cs;        // old cs

    uint32_t eflags;

    void* esp;          // old esp, 特权级升高时才有
    uint32_t ss;        // old ss,  特权级升高时才有
};

/***********  线程栈thread_stack  ***********
 * 线程自己的栈,用于存储线程中待执行的函数
 * 此结构在线程自己的内核栈中位置不固定,
 * 仅用于 switch_to 时保存线程环境。
 * 实际位置取决于实际运行情况。
 ******************************************/
struct thread_stack {
    uint32_t ebp; // ABI
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;
    
    /* 线程第一次执行时,eip指向待调用的函数kernel_thread
     其它时候,eip是指向switch_to的返回地址*/
    void (*eip) (thread_func* func, void* func_arg);
    
    /*****   以下仅第一次被调度上cpu时用到   ****/
    void (*unused_retaddr); // 参数 unused_ret 为返回地址占位
    thread_func* function;  // 由Kernel_thread所调用的函数名
    void* func_arg;         // 由Kernel_thread所调用的函数所需的参数
};

/* 进程或线程的pcb,程序控制块 */
struct task_struct {
    uint32_t*           self_kstack;    // 各线程都有自己的内核栈
    enum task_status    status;
    uint8_t             priority;       // 线程优先级，目前表现为嘀嗒数
    char                name[16];          
    
    uint8_t             ticks;  
    uint32_t            elapsed_ticks;  // 此任务自上cpu运行后至今占用了多少cpu嘀嗒数,
     
    struct list_elem    general_tag;    // 线程队列 thread_ready_list 中的结点
     
    struct list_elem    all_list_tag;   // 线程队列 thread_all_list 中的结点 
    
    uint32_t*           pgdir;          // 进程自己页表的虚拟地址
    uint32_t            stack_magic;    // 用这串数字做栈的边界标记,用于检测栈的溢出
};

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);

void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread); 



#endif

