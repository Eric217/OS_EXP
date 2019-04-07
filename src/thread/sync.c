#include "sync.h"
#include "interrupt.h"
#include "debug.h"

void sema_init(struct semaphore *sem, uint8_t value) {
	sem->value = value;
	list_init(&sem->waiters);
}

void sema_p(struct semaphore* psem) {
	enum intr_status old = intr_disable();

	while (psem->value == 0) {
		struct list_elem* cur = &(running_thread()->general_tag);
		if (elem_find(&psem->waiters, cur)) {
			PANIC("blocked thread found in waiters list \n");
		}
		list_append(&psem->waiters, cur);
		thread_block(TASK_BLOCKED);
	}
	psem->value --;
	intr_set_status(old);
}

void sema_v(struct semaphore* psem) {
	enum intr_status old = intr_disable();
	psem->value ++;
	if (!list_empty(&psem->waiters)) {
		struct list_elem* elem = list_pop(&psem->waiters);
		struct task_struct* blocked = elem2entry(struct task_struct, general_tag, elem);
		thread_unblock(blocked);
	}
	intr_set_status(old);
}

void mutex_init(struct mutex_t* pmutex) {
	pmutex->holder = NULL;
	pmutex->holder_repeat_nr = 0;
	sema_init(&pmutex->semaphore, 1);
}

void mutex_lock(struct mutex_t* pmutex) {
	struct task_struct* cur = running_thread();
	if (pmutex->holder != cur)	{
		sema_p(&pmutex->semaphore);
		pmutex->holder = cur;
		pmutex->holder_repeat_nr = 1;
	} else {
		pmutex->holder_repeat_nr ++;
	}
}

void mutex_unlock(struct mutex_t* pmutex) {
	if (pmutex->holder_repeat_nr > 1) {
		pmutex->holder_repeat_nr--;
	} else {
		ASSERT(pmutex->holder_repeat_nr == 1);
		pmutex->holder = NULL;
		pmutex->holder_repeat_nr = 0;
		sema_v(&pmutex->semaphore);
	}
}
