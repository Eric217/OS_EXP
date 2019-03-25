#include "init.h" 
#include "stdio.h"
#include "process.h"

void u_prog_a(void);

int main(void) {
    init_all();
    
    process_execute(u_prog_a, "user_prog_a");
    	
    while(1);
    return 0;
}

void u_prog_a(void) {
    
    while(1);
}
