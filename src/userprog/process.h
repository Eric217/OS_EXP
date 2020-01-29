#ifndef __USERPROG_PROCESS_H 
#define __USERPROG_PROCESS_H 
#include "thread.h"
#include "stdint.h"

#define default_prio 		31
#define USER_STACK3_VADDR   (KERNEL_SPACE - 0x1000)
#define USER_VADDR_START 	0x8048000
// elf 格式程序编译后段头表约定的第一个段代码段，段基址就是这个值

void process_execute(void* filename, char* name);
void start_process(void* filename_);
void process_activate(struct task_struct* p_thread);

void create_user_vaddr_bitmap(struct task_struct* user_prog);

void create_page_dir(struct task_struct* user_prog);
void page_dir_activate(struct task_struct* p_thread);

#endif
