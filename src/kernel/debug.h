#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H
void panic_spin(char* filename, int line, const char* func, const char* condition);
// # 将宏参数转化为字符串字面量
// 可变参数宏，括号内参数用"..."表示，宏内用 __VA_ARGS__ 代表省略号对应的参数
#define PANIC(...) panic_spin (__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef NDEBUG
    #define ASSERT(CONDITION) ((void)0)
#else
    #define ASSERT(CONDITION) if (!(CONDITION)) PANIC(#CONDITION);                  
#endif /*__NDEBUG */

#endif /*__KERNEL_DEBUG_H*/

