#include "print.h"
#include "init.h"

#include "thread.h"

void my_thread(void *);

int main(void) { 
   init_all();
 
   thread_start("name", 31, my_thread, "my_thread running");
   println(0);

   while(1);
   return 0;
} 

void my_thread(void * arg) {
	char *str = (char *)arg;
	while(1) {
		println(str);
	}
}