#include "spatialstudio/splv_threading.h"
#include "spatialstudio/splv_log.h"
#include <string.h>

//-------------------------------------------//

static void* _splv_thread_pool_thread_entry(void* arg);

//-------------------------------------------//

SPLVerror splv_thread_create(SPLVthread* thread, SPLVthreadFunc func, void* arg)
{
#ifdef _WIN32
	thread->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, &thread->threadId);
	return (thread->handle == NULL) ? SPLV_ERROR_THREADING : SPLV_SUCCESS;
#else
	return (pthread_create(&thread->handle, NULL, func, arg) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_thread_join(SPLVthread* thread, void** retval)
{
#ifdef _WIN32
	DWORD wait_result = WaitForSingleObject(thread->handle, INFINITE);
	if(wait_result != WAIT_OBJECT_0)
		return SPLV_ERROR_THREADING;
   
	if(retval)
	{
		DWORD exit_code;
		GetExitCodeThread(thread->handle, &exit_code);
		*retval = (void*)(uintptr_t)exit_code;
	}
	
	return CloseHandle(thread->handle) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#else
	return (pthread_join(thread->handle, retval) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

//-------------------------------------------//

SPLVerror splv_mutex_init(SPLVmutex* mutex)
{
#ifdef _WIN32
	InitializeCriticalSection(&mutex->cs);
	return SPLV_SUCCESS;
#else
	return (pthread_mutex_init(&mutex->mutex, NULL) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_mutex_destroy(SPLVmutex* mutex)
{
#ifdef _WIN32
	DeleteCriticalSection(&mutex->cs);
	return SPLV_SUCCESS;
#else
	return (pthread_mutex_destroy(&mutex->mutex) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_mutex_lock(SPLVmutex* mutex)
{
#ifdef _WIN32
	EnterCriticalSection(&mutex->cs);
	return SPLV_SUCCESS;
#else
	return (pthread_mutex_lock(&mutex->mutex) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_mutex_unlock(SPLVmutex* mutex)
{
#ifdef _WIN32
	LeaveCriticalSection(&mutex->cs);
	return SPLV_SUCCESS;
#else
	return (pthread_mutex_unlock(&mutex->mutex) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

//-------------------------------------------//

SPLVerror splv_condition_variable_init(SPLVconditionVariable* cond)
{
#ifdef _WIN32
	InitializeConditionVariable(&cond->cond);
	return SPLV_SUCCESS;
#else
	return (pthread_cond_init(&cond->cond, NULL) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_condition_variable_destroy(SPLVconditionVariable* cond)
{
#ifdef _WIN32
	// Windows condition variables don't need to be destroyed
	return SPLV_SUCCESS;
#else
	return (pthread_cond_destroy(&cond->cond) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_condition_variable_wait(SPLVconditionVariable* cond, SPLVmutex* mutex)
{
#ifdef _WIN32
	return SleepConditionVariableCS(&cond->cond, &mutex->cs, INFINITE) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#else
	return (pthread_cond_wait(&cond->cond, &mutex->mutex) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_condition_variable_signal_one(SPLVconditionVariable* cond)
{
#ifdef _WIN32
	WakeConditionVariable(&cond->cond);
	return SPLV_SUCCESS;
#else
	return (pthread_cond_signal(&cond->cond) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

SPLVerror splv_condition_variable_signal_all(SPLVconditionVariable* cond)
{
#ifdef _WIN32
	WakeAllConditionVariable(&cond->cond);
	return SPLV_SUCCESS;
#else
	return (pthread_cond_broadcast(&cond->cond) == 0) ? SPLV_SUCCESS : SPLV_ERROR_THREADING;
#endif
}

//-------------------------------------------//

SPLVerror splv_thread_pool_create(SPLVthreadPool** p, uint32_t numThreads, SPLVthreadPoolFunc workFunc, uint64_t workSize)
{
	//validate:
	//-----------------
	SPLV_ASSERT(numThreads > 0, "number of threads must be positive");
	SPLV_ASSERT(workSize > 0, "work struct size must be positive");	

	//allocate struct:
	//-----------------
	*p = (SPLVthreadPool*)SPLV_MALLOC(sizeof(SPLVthreadPool));
	if(!*p)
	{
		SPLV_LOG_ERROR("failed to allocate thread pool struct");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	SPLVthreadPool* pool = *p;
	memset(pool, 0, sizeof(SPLVthreadPool));

	pool->workFunc = workFunc;

	//allocate thread array:
	//-----------------
	pool->numThreads = numThreads;
	pool->threads = (SPLVthread*)SPLV_MALLOC(pool->numThreads * sizeof(SPLVthread));
	if(!pool->threads)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to allocate thread pool thread array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//create group stack:
	//-----------------
	const uint32_t WORK_STACK_CAP_INITIAL = 8;

	pool->workSize = workSize;
	pool->workStackCap = WORK_STACK_CAP_INITIAL;
	pool->workStackTop = -1;
	pool->workStack = SPLV_MALLOC(pool->workStackCap * pool->workSize);
	if(!pool->workStack)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to allocate thread pool stack");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	SPLVerror workMutexError = splv_mutex_init(&pool->workStackMutex);
	if(workMutexError != SPLV_SUCCESS)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to create work stack mutex");
		return workMutexError;
	}

	SPLVerror workCondError = splv_condition_variable_init(&pool->workStackEmptyCond);
	if(workCondError != SPLV_SUCCESS)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to create work stack empty condition variable");
		return workCondError;
	}

	//create working threads cond:
	//-----------------
	pool->numWorkProcessing = 0;

	SPLVerror workingMutexError = splv_mutex_init(&pool->workingMutex);
	if(workingMutexError != SPLV_SUCCESS)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to create working mutex");
		return workingMutexError;
	}

	SPLVerror workingCondError = splv_condition_variable_init(&pool->workDoneCond);
	if(workingCondError != SPLV_SUCCESS)
	{
		splv_thread_pool_destroy(pool);

		SPLV_LOG_ERROR("failed to create work done condition variable");
		return workingCondError;
	}

	//start threads:
	//-----------------
	pool->threadsShouldExit = 0;
	for(uint32_t i = 0; i < pool->numThreads; i++)
	{
		SPLVerror threadError = splv_thread_create(&pool->threads[i], _splv_thread_pool_thread_entry, pool);
		if(threadError != SPLV_SUCCESS)
		{
			splv_thread_pool_destroy(pool);

			SPLV_LOG_ERROR("failed to create decoder thread pool thread");
			return threadError;
		}
	}

	return SPLV_SUCCESS;
}

void splv_thread_pool_destroy(SPLVthreadPool* pool)
{
	if(!pool)
		return;

	if(pool->threads)
	{
		pool->threadsShouldExit = 1;
		if(splv_condition_variable_signal_all(&pool->workStackEmptyCond) != SPLV_SUCCESS)
			SPLV_LOG_ERROR("failed to cleanup thread pool - could not notify cond var");

		for(uint32_t i = 0; i < pool->numThreads; i++)
		{
			if(splv_thread_join(&pool->threads[i], NULL) != SPLV_SUCCESS)
				SPLV_LOG_ERROR("failed to cleanup thread pool - could not join with thread");
		}

		SPLV_FREE(pool->threads);
	}

	if(pool->workStack)
	{
		if(splv_mutex_destroy(&pool->workStackMutex) != SPLV_SUCCESS)
			SPLV_LOG_ERROR("failed to cleanup thread pool - could not destroy mutex");
		
		if(splv_condition_variable_destroy(&pool->workStackEmptyCond) != SPLV_SUCCESS)
			SPLV_LOG_ERROR("failed to cleanup thread pool - could not destroy cond var");
	
		if(splv_mutex_destroy(&pool->workingMutex) != SPLV_SUCCESS)
			SPLV_LOG_ERROR("failed to cleanup thread pool - could not destroy mutex");
	
		if(splv_condition_variable_destroy(&pool->workDoneCond) != SPLV_SUCCESS)
			SPLV_LOG_ERROR("failed to cleanup thread pool - could not destroy cond var");

		SPLV_FREE(pool->workStack);
	}

	SPLV_FREE(pool);
}

SPLVerror splv_thread_pool_add_work(SPLVthreadPool* pool, void* workItem)
{
	//TODO: error checking for mutex/cond vars? (dont think they can ever fail except for programmer error)

	splv_mutex_lock(&pool->workStackMutex);
	
	if(pool->workStackTop >= (int32_t)pool->workStackCap - 1)
	{
		uint64_t newCap = pool->workStackCap * 2;
		void* newStack = SPLV_REALLOC(pool->workStack, newCap * pool->workSize);

		if(!newStack)
		{
			splv_mutex_unlock(&pool->workStackMutex);

			SPLV_LOG_ERROR("failed to realloc work stack");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		pool->workStack = newStack;
		pool->workStackCap = newCap;
	}
	
	pool->workStackTop++;
	void* dst = (char*)pool->workStack + pool->workStackTop * pool->workSize;
	memcpy(dst, workItem, pool->workSize);
	
	splv_mutex_lock(&pool->workingMutex);
	pool->numWorkProcessing++;
	splv_mutex_unlock(&pool->workingMutex);
	
	splv_condition_variable_signal_one(&pool->workStackEmptyCond);
	
	splv_mutex_unlock(&pool->workStackMutex);
	
	return SPLV_SUCCESS;
}

SPLVerror splv_thread_pool_wait(SPLVthreadPool* pool)
{
	splv_mutex_lock(&pool->workingMutex);
	
	while(pool->numWorkProcessing > 0)
		splv_condition_variable_wait(&pool->workDoneCond, &pool->workingMutex);
	
	splv_mutex_unlock(&pool->workingMutex);
	
	return SPLV_SUCCESS;
}

//-------------------------------------------//

static void* _splv_thread_pool_thread_entry(void* arg)
{
	//TODO: we are not yet handling failure conditions. how to do this?

	SPLVthreadPool* pool = (SPLVthreadPool*)arg;

	void* job = SPLV_MALLOC(pool->workSize); //for safely accessing job before it gets overwritten
	if(!job)
	{
		SPLV_LOG_ERROR("failed to allocate job copy on worker thread");
		return NULL;
	}

	while(1)
	{
		splv_mutex_lock(&pool->workStackMutex);
		while(pool->workStackTop < 0 && pool->threadsShouldExit == 0)
			splv_condition_variable_wait(&pool->workStackEmptyCond, &pool->workStackMutex);

		if(pool->threadsShouldExit)
		{
			splv_mutex_unlock(&pool->workStackMutex);
			break;
		}

		memcpy(job, (char*)pool->workStack + pool->workStackTop * pool->workSize, pool->workSize);
		pool->workStackTop--;

		splv_mutex_unlock(&pool->workStackMutex);

		SPLVerror workError = pool->workFunc(job); //TODO handle error?

		splv_mutex_lock(&pool->workingMutex);
		
		pool->numWorkProcessing--;
		if(pool->numWorkProcessing == 0)
			splv_condition_variable_signal_one(&pool->workDoneCond);

		splv_mutex_unlock(&pool->workingMutex);
	}

	return NULL;
}