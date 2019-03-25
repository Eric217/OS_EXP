#include "stdio.h"
#include "interrupt.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "print.h"
#include "math.h"

#define va_start(ap, v) ap = (va_list)&v    // 把ap指向第一个固定参数v
#define va_arg(ap, t)   *((t*)(ap += 4))    // ap指向下一个参数并返回其值
#define va_end(ap)      ap = NULL           // 清除ap

#define UPPER 1
#define LOWER 0

/** 将整数按照进制转换成字符，并写入buf */
static void itoa(uint32_t value, char** buf_ptr_addr, uint8_t base, bool _case) {
    uint32_t m = value % base;      // 求模，低位
    uint32_t i = value / base;      // 取整，递归
    if (i)                          // i 不为 0，递归调用
        itoa(i, buf_ptr_addr, base, _case);

    if (m < 10) {                   // 余数是0~9，字符'0'~'9'
        *((*buf_ptr_addr)++) = m + '0';       
    } else {                        // 余数是A~F，字符'A'~'F'
        char b = _case == LOWER ? 'a' : 'A';
        *((*buf_ptr_addr)++) = m - 10 + b;  
    }
}

#define _itoa(value, buf_ptr_addr, base) itoa(value, buf_ptr_addr, base, LOWER) 

/** 将单精度转换成字符，并写入buf */ // 双精度也差不多这样，暂不实现
static void ftoa(float value, char** buf_ptr_addr, uint8_t after_bits) {
    // 由于我忘了阶码、尾数等东西，因此这里随便实现的
    bool neg = false;
    if (value < 0) {
        value = -value;
        neg = true;
    }
    int integer = (int)value; // 得到整数部分

    float decimal = value - integer;
    float shifted = decimal * pow(10, after_bits);
    int deci_int = (int)shifted;
    if (deci_int < (int)(0.5 + shifted)) {
        deci_int++;
    } // 得到小数部分的整数表示
    if (neg) {
        *((*buf_ptr_addr)++) = '-';
    }
    _itoa(integer, buf_ptr_addr, 10);
    if (!after_bits)
        return;
    *((*buf_ptr_addr)++) = '.';
    _itoa(deci_int, buf_ptr_addr, 10);
}

#define _ftoa(value, buf_ptr_addr) ftoa(value, buf_ptr_addr, 6)

/** 将参数ap按照格式format输出到字符串str,并返回替换后str长度 */
uint32_t vsprintf(char* str, const char* format, va_list ap) {
    char* buf_ptr = str;
    const char* index_ptr = format;
    char index_char = *index_ptr;

    int32_t arg_int; // temp
    void* arg_void;
    char* arg_char;

    while(index_char) {
        if (index_char != '%') {
            *(buf_ptr++) = index_char; 
        } else {
            index_char = *(++index_ptr);     // 得到%后面的字符
            switch(index_char) {
                case 'x': // unsigned hex
                    arg_int = va_arg(ap, int);
                    _itoa(arg_int, &buf_ptr, 16); 
                    break;
                case 'X':
                    arg_int = va_arg(ap, int);
                    itoa(arg_int, &buf_ptr, 16, UPPER); 
                    break;
                case 'o':
                    arg_int = va_arg(ap, int);
                    _itoa(arg_int, &buf_ptr, 8); 
                    break;
                case 'u':
                    arg_int = va_arg(ap, int);
                    _itoa(arg_int, &buf_ptr, 10); 
                    break;
                case 'i':
                case 'd':
                    arg_int = va_arg(ap, int);
                    if (arg_int < 0) {
                        arg_int = ~arg_int + 1;
                        *(buf_ptr++) = '-';
                    }
                    _itoa(arg_int, &buf_ptr, 10); 
                    break;
                case 'f': 
                    _ftoa(va_arg(ap, float), &buf_ptr);
                    break;
                case 'p':
                    arg_void = va_arg(ap, void*);
                    _itoa((uint32_t)arg_void, &buf_ptr, 16); 
                    break;
                case 's': 
                    arg_char = va_arg(ap, char*);
                    strcpy(buf_ptr, arg_char); 
                    buf_ptr += strlen(arg_char);
                    break;
                case 'c': 
                    *(buf_ptr++) = va_arg(ap, char);
                    break; 
                case 'e': // 指数 
                case 'l': 
                case 'h':  
                case '#':
                default: // 包括 %% 及其他未定义控制符，直接输出
                    *(buf_ptr++) = index_char; 
                    break;
            }
        }
        index_char = *(++index_ptr); // 跳过格式字符并更新index_char
    }
    return strlen(str);
}

/** 格式化打印字符串format */
uint32_t printf(const char* format, ...) {
    va_list args;
    va_start(args, format);         // 使args指向format: char* args = (char*)&format
    // TODO: - buf 的尺寸？？？
    char buf[1024] = {0};          
    vsprintf(buf, format, args);
    va_end(args);
    return write(buf);
}

/** 格式化字符串 */
uint32_t sprintf(char* buf, const char* format, ...) {
    va_list args;
    va_start(args, format);         // 使args指向format: char* args = (char*)&format
    uint32_t retval = vsprintf(buf, format, args); 
    va_end(args);
    return retval;
}
