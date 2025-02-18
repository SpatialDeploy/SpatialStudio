#include "spatialstudio/splv_threading.h"

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