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
#endif

//-------------------------------------------//

/**
 * thread function prototype
 */
typedef void* (*SPLVthreadFunc)(void*);

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
 * thread pool work function prototype
 */
typedef SPLVerror (*SPLVthreadPoolFunc)(void*);

/**
 * thread pool for simpler multithreading
 */
typedef struct SPLVthreadPool
{
	uint32_t threadsShouldExit;
	uint32_t numThreads;
	SPLVthread* threads;

	SPLVthreadPoolFunc workFunc;

	uint64_t workSize;
	int32_t workStackTop;
	uint32_t workStackCap;
	void* workStack;
	SPLVmutex workStackMutex;
	SPLVconditionVariable workStackEmptyCond;

	uint32_t numWorkProcessing;
	SPLVmutex workingMutex;
	SPLVconditionVariable workDoneCond;
} SPLVthreadPool;

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

/**
 * creates a thread pool
 */
SPLV_NOMANGLE SPLVerror splv_thread_pool_create(SPLVthreadPool** pool, uint32_t numThreads, SPLVthreadPoolFunc workFunc, uint64_t workSize);

/**
 * destroys a thread pool
 */
SPLV_NOMANGLE void splv_thread_pool_destroy(SPLVthreadPool* pool);

/**
 * pushes work on to a thread pool's stack
 */
SPLV_NOMANGLE SPLVerror splv_thread_pool_add_work(SPLVthreadPool* pool, void* workItem);

/**
 * waits for all work in a thread pool to finish
 */
SPLV_NOMANGLE SPLVerror splv_thread_pool_wait(SPLVthreadPool* pool);

#endif //#ifndef SPLV_THREADING_H