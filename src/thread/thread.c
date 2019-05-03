#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"
#include "process.h"
#include "sync.h"

struct task_struct* main_thread;    // 主线程PCB
struct task_struct* idle_thread;    // idle 线程

struct list thread_ready_list;      // 就绪队列
struct list thread_all_list;        // 所有任务队列

struct mutex_t pid_lock;

extern void switch_to(struct task_struct* cur, struct task_struct* next);

/** 获取当前线程pcb指针 */
inline struct task_struct* running_thread() {
    uint32_t esp;
    asm ("mov %%esp, %0" : "=g" (esp));
    // 栈指针 4K 取整 -> PCB 起始地址
    return (struct task_struct*)(esp & 0xfffff000);
}

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    /* 执行function前要开中断,避免后面的时钟中断被屏蔽,而无法调度其它线程 */
    intr_enable();
    function(func_arg);
    // 接下来 永久把这个线程消灭，我猜测的流程是：
    // TASK_DIED
    // SCHEDULE
    // 有专门的线程负责把 DIED TASK 从链表中移除；
    // 回收 kmemory。Linux 中好像直接做了 Cache，如 始终有65536个PCB
    intr_disable();
    running_thread()->status = TASK_DIED;
    schedule();
}

static void idle(__attribute__((unused)) void* arg) {
    while (1) {
        thread_block(TASK_BLOCKED);
        // 执行 hlt 时必须要保证目前处在开中断的情况下
        asm volatile ("sti; hlt" : : : "memory");
    }
}

static pid_t allocate_pid() {
    static pid_t next_pid = 0; 
    mutex_lock(&pid_lock);
    next_pid ++;
    mutex_unlock(&pid_lock);
    return next_pid;
}

void thread_block(enum task_status stat) {
    // 3 status are allowed
    ASSERT(stat == TASK_BLOCKED || stat == TASK_WAITING || stat == TASK_HANGING);
    enum intr_status old_status = intr_disable();
    running_thread()->status = stat;
    schedule();
    intr_set_status(old_status);
}

void thread_yield() {
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status); 
}

void thread_unblock(struct task_struct* pthread) {
    enum intr_status old_status = intr_disable();
    enum task_status p_stat = pthread->status;
    ASSERT(p_stat == TASK_HANGING || p_stat == TASK_WAITING || p_stat == TASK_BLOCKED);
    if (p_stat != TASK_READY) {
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("find blocked thread in ready list\n");
        }
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    } 
    intr_set_status(old_status); 
}
  
/** 初始化线程栈thread_stack：保存任务函数和参数 */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
    
    pthread->self_kstack -= sizeof(struct intr_stack); // 为了启动用户进程预留，线程用不到。其实这一项在使用时也是可以不用的...
    pthread->self_kstack -= sizeof(struct thread_stack); // 为了启动线程预留 
    
    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);
      
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE); // 内核栈顶在页表顶部
    // TODO: - 一个进程一个 pid 啊，需要修改（配合那个用户进程里的多线程）
    pthread->pid = allocate_pid();
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;

    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    for (int i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i) {
        pthread->fd_table[i] = -1;
    }
    
    pthread->stack_magic = 0x19870916;  // 自定义的魔数

    pthread->status = TASK_READY;
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING; // main 线程一直是运行的
    }
}

/* 创建一优先级为prio的线程,线程名为name,线程所执行的函数是function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    /* pcb都位于内核空间,包括用户进程的pcb也是在内核空间 */
    struct task_struct* thread = get_pages(1, PF_KERNEL);
    
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);
    
    /* 确保之前不在队列中 */
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    /* 加入就绪线程队列 */
    list_append(&thread_ready_list, &thread->general_tag);
    
    /* 确保之前不在队列中 */
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    /* 加入全部线程队列 */
    list_append(&thread_all_list, &thread->all_list_tag);
    
    return thread;
}

/* 将kernel中的main函数完善为主线程 */
static void make_main_thread(void) {
    /* 因为main线程早已运行,在loader.S中进入内核时的mov esp,0xc009f000,
     就是为其预留了tcb,地址为0xc009e000,因此不需要通过get_kernel_page另分配一页*/
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);
    
    /* main函数是当前线程,当前线程不在thread_ready_list中,
     * 所以只将其加在thread_all_list中. */
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

static void make_idle_thread() {
    idle_thread = thread_start("idle", 10, idle, NULL); 
}

static struct list_elem* thread_tag; // temp container

/* 实现任务调度 */
void schedule() {
    
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct* cur = running_thread();
    if (cur->status == TASK_RUNNING) { // 若此线程只是cpu时间片到了,将其加入到就绪队列尾
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;     // 重新将当前线程的ticks再重置为其priority;
        cur->status = TASK_READY;
    } else {
        /* 若此线程需要某事件发生后才能继续上cpu运行,
         不需要将其加入队列,因为当前线程不在就绪队列中。*/
    }

    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }

    /* 将thread_ready_list队列中的第一个就绪线程弹出,准备将其调度上cpu. */
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    process_activate(next);

    switch_to(cur, next);
}

/* 初始化线程环境 */
void thread_init(void) {
    put_str("   thread_init start...\n");

    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    mutex_init(&pid_lock);

    make_main_thread(); // 将当前main函数创建为线程
    make_idle_thread(); // 启动 idle 线程
    
    put_str("   thread_init done!\n");
}


