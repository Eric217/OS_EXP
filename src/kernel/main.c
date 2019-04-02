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

    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);

    printf(" prog_a malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

    free(addr1);
    free(addr2);
    free(addr3);
    
    while(1);
}
