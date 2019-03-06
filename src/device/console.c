#include "print.h"
#include "sync.h"
#include "console.h"

static struct mutex_t console_mutex;

void console_init() {
	mutex_init(&console_mutex);
}

void console_lock() {
	mutex_lock(&console_mutex);
}

void console_unlock() {
	mutex_unlock(&console_mutex);
}

void console_put_str(char *str) {
	console_lock();
	put_str(str);
	console_unlock();
}

void console_put_char(uint8_t ch) {
	console_lock();
	put_char(ch);
	console_unlock();
}

void console_put_int(uint32_t num) {
	console_lock();
	put_int(num);
	console_unlock();
}

