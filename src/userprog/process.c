#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "list.h"
#include "tss.h"
#include "interrupt.h"
#include "string.h"

extern void intr_exit(void);

// 由 kernel_thread 调用 function(args)
/** 构建用户进程初始上下文信息 */
void start_process(void* filename_) {
    // 加载程序文件到内存
    // 解析程序，得到入口地址 function
    // 目前先跳过
    void* function = filename_;

    struct task_struct* cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;

    // 伪造中断返回 iretd 环境，线程是伪造调用返回
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs  = 0;         // 用户态用不上,直接初始为0。想用的话，把显存段 DPL 设为3
    proc_stack->ds  = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function;     
    // 每个进程有自己的一套地址空间，same pattern，此时已切换页表
    proc_stack->cs  = SELECTOR_U_CODE;
    proc_stack->esp = (void*)((uint32_t)get_one_page(PF_USER, USER_STACK3_VADDR)+PG_SIZE);
    if (!proc_stack->esp) {
        ASSERT(false);
        // 1. 有成熟的页置换系统，根本不会出现这种情况；
        // 2. 告知父任务 子任务创建失败并回收资源
    }
    proc_stack->ss  = SELECTOR_U_DATA;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);

    // 替换 esp，跳转到 intr_exit，恢复一系列寄存器，进入3级
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

/* 激活页表 */
inline void page_dir_activate(struct task_struct* p_thread) {
    // TODO: - 用户级新创建的线程 页表为空，需要完善，这里需要完全重写 
    // 思考： 每个task struct 都有 pgdir，线程创建时 值同当前进程（内核线程除外）

    // 若为内核线程，页表地址 0x100000
    uint32_t pagedir_phy_addr = 0x100000;  
    if (p_thread->pgdir != NULL)    {   
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }
    
    // 更新页目录寄存器cr3,使新页表生效
    asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

/* 激活线程或进程的页表,更新tss中的esp0为进程的特权级0的栈 */
void process_activate(struct task_struct* p_thread) {
   
    // 激活该进程或线程的页表
    page_dir_activate(p_thread);
    
    // 内核级任务中断后直接用当时的栈，不会从tss中获取0特权级栈地址。可以更新esp0，但没必要
    // 加了用户任务之后，为了区分内核任务，再想条件判断
    if (p_thread->pgdir) {
        /* 更新该进程的esp0,用于此进程被中断时保留上下文 */
        update_tss_esp(p_thread);
    }
}

/* 创建页目录表,将当前页表的表示内核空间的pde复制,
 * 成功则返回页目录的虚拟地址,否则返回-1 */
void create_page_dir(struct task_struct* user_prog) {
    
    // 用户进程的页表不能让用户直接访问到,所以在内核空间来申请
    uint32_t* page_dir_vaddr = get_pages(1, PF_KERNEL);
    if (page_dir_vaddr == NULL) {
        ASSERT(false); 
    }
    
    // 所有进程共用内核1GB 对应的256个页表，0x300*4 是页目录表第768项，共复制 256*4 字节
    memcpy( (uint32_t*)((uint32_t)page_dir_vaddr + 0x300*4), 
            (uint32_t*)(0xfffff000+0x300*4), 
             1024);
    // 页目录表第1023项存的地址指向本身 更新页目录表物理地址 
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    user_prog->pgdir = page_dir_vaddr;
}

/** 创建用户进程虚拟地址池：start addr，bitmap */
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;

    // 3GB - START 字节，除以 页尺寸，是位图的 位数，除以8是位图需要的字节数，
    // 再除以 页尺寸、取整，即位图需要从内核池中分配的页数
    uint32_t bitmap_bytes_cnt = (KERNEL_SPACE - USER_VADDR_START) / PG_SIZE / 8;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP(bitmap_bytes_cnt, PG_SIZE);
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_pages(bitmap_pg_cnt, PF_KERNEL);  
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = bitmap_bytes_cnt;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/* 创建用户进程 */
void process_execute(void* filename, char* name) {
    // 由内核维护所有PCB，在内核池中申请
    struct task_struct* thread = get_pages(1, PF_KERNEL);
    init_thread(thread, name, default_prio);
    create_page_dir(thread);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);

    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}

