#ifdef _MSC_VER

#include "cpthreads.h"

#include <stdio.h>

// Variables for Thread Specific Storage
static tss_dtor_t* destructor_stack = NULL;
static int destructor_stack_count = 0;

static void thread_specific_storage_cleanup(void) {
    for(int i = 0; i < destructor_stack_count; i++) {
        if(!destructor_stack[i])
            continue;
        void* value = TlsGetValue(i);
        if(value != NULL || GetLastError() == ERROR_SUCCESS)
            destructor_stack[i](value);
    }
}

struct ___cp_thrd_state {
    thrd_start_t func;
    void* arg;
};

unsigned int __stdcall ___cp_thrd_func(struct ___cp_thrd_state* state) {
    // Call the user supplied function
    unsigned int result = (unsigned int)state->func(state->arg);

    // If the thread doesn't exit via thrd_exit,
    // clean up the thread specific data.
    // Otherwise, thrd_exit handles it.
    thread_specific_storage_cleanup();

    return (int)result;
}

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
    thr->state.arg = arg;
    thr->state.func = func;
    thr->handle = (HANDLE)_beginthreadex(NULL, 
                                         0, 
                                         (unsigned (__stdcall *)(void*))___cp_thrd_func, 
                                         &thr->state, 
                                         0, 
                                         NULL);
    if(!thr->handle)
        return errno == EACCES ? thrd_nomem : thrd_error;
    else
        return thrd_success;
}

#ifdef CP_ACCURATE_SLEEP
// This isn't used, but if you're looking for a more accurate sleep
// function you could alter the header to use this instead.
// The code can mostly be found here: 
// https://stackoverflow.com/a/31411628
// I'm not 100% the conversion from struct timespec to long long is entirely accurate
// The result is the desired time to sleep in units of 100 nanoseconds.

static NTSTATUS(__stdcall *NtDelayExecution)(BOOL Alertable, PLARGE_INTEGER DelayInterval);
static NTSTATUS(__stdcall *ZwSetTimerResolution)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution);

static once_flag sleep_flag = ONCE_FLAG_INIT;
static void thrd_sleep_init(void) {
    NtDelayExecution = (NTSTATUS(__stdcall*)(BOOL, PLARGE_INTEGER)) GetProcAddress(GetModuleHandle("ntdll.dll"), "NtDelayExecution");
    ZwSetTimerResolution = (NTSTATUS(__stdcall*)(ULONG, BOOLEAN, PULONG)) GetProcAddress(GetModuleHandle("ntdll.dll"), "ZwSetTimerResolution");
    ULONG actualResolution;
    ZwSetTimerResolution(1, TRUE, &actualResolution);
}

int thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    call_once(&sleep_flag, thrd_sleep_init);
    LARGE_INTEGER interval;
    interval.QuadPart = -1 * (long long)(duration->tv_sec * 10000000) + (long long)(duration->tv_nsec / 100);
    if(NtDelayExecution(FALSE, &interval) != 0)
        return thrd_error;
    return thrd_success;
}

#endif

void thrd_exit(int res) {
    thread_specific_storage_cleanup();
    _endthreadex((unsigned int)res);
}

int thrd_join(thrd_t thr, int* res) {
    unsigned result;
    switch((result = WaitForSingleObject(thr.handle, INFINITE))) {
        case WAIT_OBJECT_0:
        case WAIT_ABANDONED:
        {
            DWORD exit_code;
            if(GetExitCodeThread(thr.handle, &exit_code)) {
                if(res != NULL) *res = (int)exit_code;
                CloseHandle(thr.handle);
                return thrd_success;
            }
        }
        // Fall through on purpose
        default:
            CloseHandle(thr.handle);
            return thrd_error;
    }
}

int mtx_init(mtx_t* mutex, int type) {
    if(!mutex || type == mtx_recursive)
        return thrd_error;

    mutex->type = type;
    switch(type) {
        case mtx_plain:
            InitializeSRWLock(&mutex->lock);
            break;
        case mtx_timed:
            mutex->handle = CreateSemaphore(NULL, 1, 1, NULL);
            if (mutex->handle == NULL) {
                mutex->type = 0;
                return thrd_error;
            }
            break;
        case mtx_plain | mtx_recursive:
            InitializeCriticalSection(&mutex->section);
            break;
        case mtx_timed | mtx_recursive:
            mutex->handle = CreateMutex(NULL, FALSE, NULL);
            if (mutex->handle == NULL) {
                mutex->type = 0;
                return thrd_error;
            }
            break;
        default:
            return thrd_error;
    }
    return thrd_success;
}

int mtx_lock(mtx_t* mutex) {
    if(!mutex)
        return thrd_error;

    switch(mutex->type) {
        case mtx_plain:
            AcquireSRWLockExclusive(&mutex->lock);
            break;
        case mtx_plain | mtx_recursive:
            EnterCriticalSection(&mutex->section);
            break;
        case mtx_timed:
        case mtx_timed | mtx_recursive:
            switch(WaitForSingleObject(mutex->handle, INFINITE)) {
                case WAIT_ABANDONED:
                case WAIT_OBJECT_0:
                    return thrd_success;
                default:
                    return thrd_error;
            }
    }

    return thrd_success;
}

int mtx_timedlock(mtx_t* mutex, const struct timespec* time_point) {
    if(!mutex || (mutex->type & mtx_timed) != mtx_timed || !mutex->handle)
        return thrd_error;

    DWORD ms = (DWORD) (time_point->tv_sec * 1000 + time_point->tv_nsec / 1000000);
    switch(WaitForSingleObject(mutex->handle, ms)) {
        case WAIT_ABANDONED:
        case WAIT_OBJECT_0:
            return thrd_success;
        case WAIT_TIMEOUT:
            return thrd_timedout;
        default:
            return thrd_error;
    }
}

int mtx_trylock(mtx_t* mutex) {
    if(!mutex)
        return thrd_error;

    switch(mutex->type) {
        case mtx_plain:
            if(!TryAcquireSRWLockExclusive(&mutex->lock))
                return thrd_busy;
            break;
        case mtx_plain | mtx_recursive:
            if(!TryEnterCriticalSection(&mutex->section))
                return thrd_busy;
            break;
        case mtx_timed:
        case mtx_timed | mtx_recursive:
            switch(WaitForSingleObject(mutex->handle, 0)) {
                case WAIT_ABANDONED:
                case WAIT_OBJECT_0:
                    return thrd_success;
                case WAIT_TIMEOUT:
                    return thrd_busy;
                default:
                    return thrd_error;
            }
    }

    return thrd_success;
}

int mtx_unlock(mtx_t* mutex) {
    if(!mutex)
        return thrd_error;

    switch(mutex->type) {
        case mtx_plain:
            ReleaseSRWLockExclusive(&mutex->lock);
            break;
        case mtx_plain | mtx_recursive:
            LeaveCriticalSection(&mutex->section);
            break;
        case mtx_timed:
            if(!ReleaseSemaphore(mutex->handle, 1, NULL))
                return thrd_error;
            break;
        case mtx_timed | mtx_recursive:
            if(!ReleaseMutex(mutex->handle))
                return thrd_error;
            break;
    }

    return thrd_success;
}

void mtx_destroy(mtx_t* mutex) {
    if(!mutex)
        return;

    switch(mutex->type) {
        case mtx_plain:
            break;
        case mtx_plain | mtx_recursive:
            DeleteCriticalSection(&mutex->section);
            break;
        case mtx_timed:
        case mtx_timed | mtx_recursive:
            CloseHandle(mutex->handle);
    }
}

int cnd_init(cnd_t* cond) {
    InitializeCriticalSection(&cond->lock);
    InitializeConditionVariable(&cond->variable);
    cond->queue = 0;
    cond->sem = NULL;
    cond->waiters = 0;
    cond->shift = -1;
    return thrd_success;
}

int cnd_signal(cnd_t* cond) {
    EnterCriticalSection(&cond->lock);
    BOOLEAN result = TRUE;

    if(cond->shift == -1)
        goto RESULT;

    if((cond->queue & (1 << cond->shift)) == 0)
        WakeConditionVariable(&cond->variable);
    else {
        cond->waiters--;
        result = ReleaseSemaphore(cond->sem, 1, NULL);
    }

    LeaveCriticalSection(&cond->lock);

    RESULT:
    return result ? thrd_success : thrd_error;
}

int cnd_broadcast(cnd_t* cond) {
    EnterCriticalSection(&cond->lock);
    BOOLEAN result = TRUE;

    if(cond->shift == -1)
        goto RESULT;

    WakeAllConditionVariable(&cond->variable);
    if(cond->waiters > 0) {
        result = ReleaseSemaphore(cond->sem, cond->waiters, NULL);
        cond->waiters = 0;
    }

    LeaveCriticalSection(&cond->lock);

    RESULT:
    return result ? thrd_success : thrd_error;
}

static int ___cnd_wait_impl(cnd_t* cond, mtx_t* mutex, DWORD ms) {
    EnterCriticalSection(&cond->lock);

    int result = TRUE;

    if(cond->shift + 1 == 64){
        LeaveCriticalSection(&cond->lock);
        return thrd_error;
    }

    cond->shift++;

    switch(mutex->type) {
        case mtx_plain:
            cond->queue &= ~(1ull << cond->shift);
            LeaveCriticalSection(&cond->lock);
            result = SleepConditionVariableSRW(&cond->variable, &mutex->lock, ms, 0) ? thrd_success : thrd_error;
        case mtx_plain | mtx_recursive:
            cond->queue &= ~(1ull << cond->shift);
            LeaveCriticalSection(&cond->lock);
            result = SleepConditionVariableCS(&cond->variable, &mutex->section, ms) ? thrd_success : thrd_error;
            break;
        case mtx_timed:
            if(cond->sem == NULL) {
                cond->sem = CreateSemaphore(NULL, 0, INFINITE, NULL);
                if(!cond->sem) {
                    LeaveCriticalSection(&cond->lock);
                    return thrd_error;
                }
            }
            cond->queue |= (1ull << cond->shift);
            cond->waiters++;
            LeaveCriticalSection(&cond->lock);
            ReleaseSemaphore(mutex->handle, 1, NULL);
            switch(WaitForSingleObject(cond->sem, ms)) {
                case WAIT_ABANDONED:
                case WAIT_OBJECT_0:
                    result = thrd_success;
                    break;
                case WAIT_TIMEOUT:
                    result = thrd_timedout;
                    break;
                default:
                    result = FALSE;
            }
            WaitForSingleObject(mutex->handle, INFINITE);
            break;
        case mtx_timed | mtx_recursive:
            if(cond->sem == NULL) {
                cond->sem = CreateSemaphore(NULL, 0, INFINITE, NULL);
                if(!cond->sem) {
                    LeaveCriticalSection(&cond->lock);
                    return thrd_error;
                }
            }
            cond->queue |= (1ull << cond->shift);
            cond->waiters++;
            LeaveCriticalSection(&cond->lock);
            ReleaseMutex(mutex->handle);
            switch(WaitForSingleObject(cond->sem, ms)) {
                case WAIT_ABANDONED:
                case WAIT_OBJECT_0:
                    result = thrd_success;
                    break;
                case WAIT_TIMEOUT:
                    result = thrd_timedout;
                    break;
                default:
                    result = FALSE;
            }
            WaitForSingleObject(mutex->handle, INFINITE);
            break;
    }

    return result;
}

int cnd_wait(cnd_t* cond, mtx_t* mutex) {
    return ___cnd_wait_impl(cond, mutex, INFINITE);
}

int cnd_timedwait(cnd_t* cond, mtx_t* mutex, const struct timespec* time_point) {
    DWORD ms = (DWORD) (time_point->tv_sec * 1000 + time_point->tv_nsec / 1000000);
    return ___cnd_wait_impl(cond, mutex, ms);
}

void cnd_destroy(cnd_t* cond) {
    DeleteCriticalSection(&cond->lock);
    if(cond->sem != NULL) {
        CloseHandle(cond->sem);
        cond->sem = NULL;
    }
}

static once_flag destructor_stack_flag = ONCE_FLAG_INIT;
static CRITICAL_SECTION destructor_stack_lock;

static void destructor_stack_init(void) {
    InitializeCriticalSection(&destructor_stack_lock);
}

int tss_create(tss_t* tss_key, tss_dtor_t destructor) {
    call_once(&destructor_stack_flag, destructor_stack_init);

    int index = TlsAlloc();
    if(index == TLS_OUT_OF_INDEXES)
        return thrd_error;

    if(destructor != NULL) {
        EnterCriticalSection(&destructor_stack_lock);
        if(destructor_stack_count <= index) {
            if(!destructor_stack) {
                destructor_stack_count = 4;
                destructor_stack = calloc(4, sizeof(*destructor_stack));
            } else {
                while(destructor_stack_count <= index) {
                    int old = destructor_stack_count;
                    destructor_stack_count *= 2;
                    destructor_stack = realloc(destructor_stack, destructor_stack_count);
                    for(int i = old; i < destructor_stack_count; i++)
                        destructor_stack[i] = NULL;
                }
            }
        }
        destructor_stack[index] = destructor;
        LeaveCriticalSection(&destructor_stack_lock);
    }

    *tss_key = index;

    return thrd_success;
}

void tss_delete(tss_t tss_key) {
    if(TlsFree(tss_key)) {
        if(tss_key < destructor_stack_count)
            destructor_stack[tss_key] = NULL;
    }
}

#endif