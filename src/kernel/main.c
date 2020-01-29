#include "init.h"
#include "console.h"
#include "process.h"
#include "syscall.h"
#include "stdio.h"
  
void u_prog_a(void); 

int main(void) {
  
    init_all();
    
    process_execute(u_prog_a, "u_prog_a");
  
    while(1);
    return 0;
}

/* 测试用户进程 */
void u_prog_a(void) {
 
 
    while(1);
}
