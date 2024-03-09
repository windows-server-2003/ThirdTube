#include "headers.hpp"
#ifndef _POSIX_THREADS
#	define _POSIX_THREADS
#endif
#include <pthread.h>
#include "fake_pthread.hpp"

int util_fake_pthread_core_offset = 0;
int util_fake_pthread_enabled_core_list[4] = { 0, 1, -3, -3, };
int util_fake_pthread_enabled_cores = 2;

void Util_fake_pthread_set_enabled_core(bool enabled_core[4])
{
	int num_of_core = 0;

	if(!enabled_core[0] && !enabled_core[1] && !enabled_core[2] && !enabled_core[3])
		return;

	for(int i = 0; i < 4; i++)
		util_fake_pthread_enabled_core_list[i] = -3;

	for(int i = 0; i < 4; i++)
	{
		if(enabled_core[i])
		{
			util_fake_pthread_enabled_core_list[num_of_core] = i;
			num_of_core++;
		}
	}
	util_fake_pthread_enabled_cores = num_of_core;
	util_fake_pthread_core_offset = 0;
}

int	pthread_mutex_lock(pthread_mutex_t *mutex) {
	LightLock_Lock(&mutex->normal);
	return 0;
}
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
	int res = LightLock_TryLock(&mutex->normal);
	return res ? EBUSY : 0;
}
int	pthread_mutex_unlock(pthread_mutex_t *mutex)  {
	LightLock_Unlock(&mutex->normal);
	return 0;
}
int	pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
	LightLock_Init(&mutex->normal);
	return 0;
}
int	pthread_mutex_destroy(pthread_mutex_t *mutex) { // LightLock doesn't need any resource releasing
	return 0;
}

#define PTHREAD_ONCE_NOT_RUN 0
#define PTHREAD_ONCE_RUNNING 1
#define PTHREAD_ONCE_FINISHED 2

int	pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
	s32 val;
	s32 next;
	do {
		val = __ldrex((s32 *) &once_control->status);
		if (val == PTHREAD_ONCE_NOT_RUN) next = PTHREAD_ONCE_RUNNING;
		else next = val;
	} while (__strex((s32 *) &once_control->status, val));
	
	if (val == PTHREAD_ONCE_NOT_RUN) {
		init_routine();
		once_control->status = PTHREAD_ONCE_FINISHED;
	}
	__dmb();
	return 0;
}


int	pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t* attr) {
	CondVar_Init((CondVar *) &cond->cond);
	return 0;
}
int	pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
	CondVar_Wait((CondVar *) &cond->cond, &mutex->normal);
	return 0;
}

int	pthread_cond_signal(pthread_cond_t *cond) {
	CondVar_Signal((CondVar *) &cond->cond);
	return 0;
}
int	pthread_cond_broadcast(pthread_cond_t *cond) {
	CondVar_Broadcast((CondVar *) &cond->cond);
	return 0;
}
int	pthread_cond_destroy(pthread_cond_t *cond) {
	return 0;
}


int	pthread_create(pthread_t *thread, const pthread_attr_t  *attr, void *(*start_routine)(void *), void *arg)
{
	Thread handle = 0;

	if(util_fake_pthread_enabled_cores == 0)
		return -1;

	handle = threadCreate((ThreadFunc)(void *) start_routine, arg, DEF_STACKSIZE, DEF_THREAD_PRIORITY_LOW,
		util_fake_pthread_enabled_core_list[util_fake_pthread_core_offset], true);
	*thread = (pthread_t) handle;

	if(util_fake_pthread_core_offset + 1 < util_fake_pthread_enabled_cores)
		util_fake_pthread_core_offset++;
	else
		util_fake_pthread_core_offset = 0;

	if (!handle) return -1;
	else return 0;
}

int	pthread_join(pthread_t thread, void **__value_ptr) {
	while (true) {
		int result = threadJoin((Thread) thread, U64_MAX);
		if (result == 0) return 0;
	}
}

int pthread_attr_init(pthread_attr_t *attr) {
	if (!attr) return -1;

	attr->stackaddr = NULL;
	attr->stacksize = DEF_STACKSIZE;
	attr->schedparam.sched_priority = DEF_THREAD_PRIORITY_LOW;
	attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
	if (!attr || stacksize < 16384) return -1;

	attr->stacksize = stacksize;
	return 0;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
	*memptr = memalign(alignment, size);
	if (!*memptr) return -1;
	else return 0;
}

long sysconf(int name) {
	if (name == _SC_NPROCESSORS_CONF) {
		if(var_model == CFG_MODEL_N2DSXL || var_model == CFG_MODEL_N3DS || var_model == CFG_MODEL_N3DSXL)
			return 4;
		else
			return 2;
	} else if(name == _SC_NPROCESSORS_ONLN) {
		if(var_model == CFG_MODEL_N2DSXL || var_model == CFG_MODEL_N3DS || var_model == CFG_MODEL_N3DSXL)
			return 2 + var_core2_available + var_core3_available;
		else
			return 2;
	} else return -1;
}
