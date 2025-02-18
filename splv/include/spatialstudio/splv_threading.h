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

/**
 * creates a thread
 */
SPLV_NOMANGLE SPLVerror splv_thread_create(SPLVthread* thread, SPLVthreadFunc func, void* arg);

/**
 * joins with a thread
 */
SPLV_NOMANGLE SPLVerror splv_thread_join(SPLVthread* thread, void** retval);

/**
 * creates a mutex
 */
SPLV_NOMANGLE SPLVerror splv_mutex_init(SPLVmutex* mutex);

/**
 * destroys a mutex
 */
SPLV_NOMANGLE SPLVerror splv_mutex_destroy(SPLVmutex* mutex);

/**
 * locks a mutex
 */
SPLV_NOMANGLE SPLVerror splv_mutex_lock(SPLVmutex* mutex);

/**
 * unlocks a mutex
 */
SPLV_NOMANGLE SPLVerror splv_mutex_unlock(SPLVmutex* mutex);

/**
 * initializes a condition variable
 */
SPLV_NOMANGLE SPLVerror splv_condition_variable_init(SPLVconditionVariable* cond);

/**
 * destroys a condition variable
 */
SPLV_NOMANGLE SPLVerror splv_condition_variable_destroy(SPLVconditionVariable* cond);

/**
 * waits on a condition variable
 */
SPLV_NOMANGLE SPLVerror splv_condition_variable_wait(SPLVconditionVariable* cond, SPLVmutex* mutex);

/**
 * wakes a single thread waiting on a condition variable
 */
SPLV_NOMANGLE SPLVerror splv_condition_variable_signal_one(SPLVconditionVariable* cond);

/**
 * wakes all threads waiting on a condition variable
 */
SPLV_NOMANGLE SPLVerror splv_condition_variable_signal_all(SPLVconditionVariable* cond);

#endif //#ifndef SPLV_THREADING_H