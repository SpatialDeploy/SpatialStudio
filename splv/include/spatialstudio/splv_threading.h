/* splv_threading.h
 *
 * contains helper functions for platform-independent threading
 */

#ifndef SPLV_THREADING_H
#define SPLV_THREADING_H

#include <stdint.h>
#include "splv_error.h"

#ifdef _WIN32
	#include <windows.h>
#else
	#include <pthread.h>
	#include <stdatomic.h>
#endif

//-------------------------------------------//

/**
 * a thread handle
 */
typedef struct SPLVthread
{
#ifdef _WIN32
	HANDLE handle;
	DWORD threadId;
#else
	pthread_t handle;
#endif
} SPLVthread;

/**
 * a mutex
 */
typedef struct SPLVmutex 
{
#ifdef _WIN32
	CRITICAL_SECTION cs;
#else
	pthread_mutex_t mutex;
#endif
} SPLVmutex;

/**
 * a condition variable
 */
typedef struct SPLVconditionVariable
{
#ifdef _WIN32
	CONDITION_VARIABLE cond;
#else
	pthread_cond_t cond;
#endif
} SPLVconditionVariable;


/**
 * thread function prototype
 */
typedef void* (*SPLVthreadFunc)(void*);

//-------------------------------------------//

SPLVerror splv_thread_create(SPLVthread* thread, SPLVthreadFunc func, void* arg);
SPLVerror splv_thread_detach(SPLVthread* thread);
SPLVerror splv_thread_join(SPLVthread* thread, void** retval);

SPLVerror splv_mutex_init(SPLVmutex* mutex);
SPLVerror splv_mutex_destroy(SPLVmutex* mutex);
SPLVerror splv_mutex_lock(SPLVmutex* mutex);
SPLVerror splv_mutex_unlock(SPLVmutex* mutex);

SPLVerror splv_condition_variable_init(SPLVconditionVariable* cond);
SPLVerror splv_condition_variable_destroy(SPLVconditionVariable* cond);
SPLVerror splv_condition_variable_wait(SPLVconditionVariable* cond, SPLVmutex* mutex);
SPLVerror splv_condition_variable_signal_one(SPLVconditionVariable* cond);
SPLVerror splv_condition_variable_signal_all(SPLVconditionVariable* cond);

#endif //#ifndef SPLV_THREADING_H