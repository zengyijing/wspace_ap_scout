#ifndef PTHREAD_WRAPPER_H_
#define PTHREAD_WRAPPER_H_ 

#include <assert.h>
#include <pthread.h>

inline void Pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	int rc = pthread_mutex_init(mutex, attr);
	assert(rc == 0);
}

inline void Pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int rc = pthread_mutex_lock(mutex);
	assert(rc == 0);
}

inline void Pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int rc = pthread_mutex_unlock(mutex);
	assert(rc == 0);
}

inline void Pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	int rc = pthread_mutex_destroy(mutex);
	assert(rc == 0);
}

inline void Pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	int rc = pthread_cond_init(cond, attr);
	assert(rc == 0);
}

inline void Pthread_cond_destroy(pthread_cond_t * cond)
{
	int rc = pthread_cond_destroy(cond);
	assert(rc == 0);
}

inline void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	int rc = pthread_cond_wait(cond, mutex);
	assert(rc == 0);
}

inline void Pthread_cond_signal(pthread_cond_t *cond)
{
	int rc = pthread_cond_signal(cond);
	assert(rc == 0);
}

inline int Pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
	int rc = pthread_create(thread, attr, start_routine, arg);
	assert(rc == 0);
}

inline int Pthread_join(pthread_t thread, void **value_ptr)
{
	int rc = pthread_join(thread, value_ptr);
	assert(rc == 0);
}

#endif
