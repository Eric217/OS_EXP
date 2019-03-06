#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "list.h"
#include "stdint.h"
#include "thread.h"

struct semaphore {
	uint8_t	value;
	struct  list waiters; 
};

struct mutex_t {
	struct task_struct* holder;
	struct semaphore semaphore;
	uint32_t holder_repeat_nr;	
};

void sema_init(struct semaphore *sem, uint8_t value);

void sema_p(struct semaphore* psem);

void sema_v(struct semaphore* psem);


void mutex_init(struct mutex_t* pmutex);

void mutex_lock(struct mutex_t* pmutex);

void mutex_unlock(struct mutex_t* pmutex);


#endif