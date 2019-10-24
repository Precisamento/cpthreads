/*
    MIT License

    Copyright (c) 2019 Precisamento
    
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifdef _MSC_VER

#include "cpthreads.h"

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

// Helper function to subtract timespecs properly. Doesn't handle overflow.
static void timespec_subtract(const struct timespec* left, const struct timespec* right, struct timespec* result) {
    struct timespec temp = *left;
    // Use a loop in order to fix malformed timespecs.
    while(temp.tv_nsec - right->tv_nsec < 0) {
        temp.tv_sec -= 1;
        temp.tv_nsec += 1000000000L;
    }
    temp.tv_sec -= right->tv_sec;
    temp.tv_nsec -= right->tv_nsec;
    *result = temp;
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
    if(!thr)
        return thrd_error;
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

#include <stdio.h>

int thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    if(!duration)
        return thrd_error;

    // Initialize the kernel functions once per application.
    call_once(&sleep_flag, thrd_sleep_init);

    LARGE_INTEGER interval;
    interval.QuadPart = (long long)(duration->tv_sec * 10000000LL) + (long long)(duration->tv_nsec / 100LL);

    // For some reason multiplying the -1 with the previous operation wasn't always negating it,
    // but moving it to it's own worked.
    interval.QuadPart *= -1;
    if(NtDelayExecution(FALSE, &interval) != 0) {
        return thrd_error;
    }
    return thrd_success;
}

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
            if(!mutex->handle)
                return thrd_error;
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

    struct timespec current;
    timespec_get(&current, TIME_UTC);
    timespec_subtract(&current, time_point, &current);

    if(current.tv_sec < 0)
        return thrd_timedout;

    DWORD ms = (DWORD) (current.tv_sec * 1000 + current.tv_nsec / 1000000);
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
            if(!mutex->handle)
                return thrd_error;
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
            if(!mutex->handle)
                return thrd_error;
            if(!ReleaseSemaphore(mutex->handle, 1, NULL))
                return thrd_error;
            break;
        case mtx_timed | mtx_recursive:
            if(!mutex->handle)
                return thrd_error;
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
            if(!mutex->handle)
                return;
            CloseHandle(mutex->handle);
            mutex->handle = NULL;
    }
}

enum {
    CONDITION_USE_VARIABLE = 0,
    CONDITION_USE_LOCK = 1
};

#define CONDITION_NO_WAITERS -1

int cnd_init(cnd_t* cond) {
    if(!cond)
        return thrd_error;
    InitializeCriticalSection(&cond->queue_lock);
    InitializeConditionVariable(&cond->variable);
    cond->handles = NULL;
    cond->queue = 0;
    cond->shift = CONDITION_NO_WAITERS;
    cond->handle_count = 0;
    cond->handle_cap = 0;
    return thrd_success;
}

int cnd_signal(cnd_t* cond) {
    if(!cond)
        return thrd_error;

    EnterCriticalSection(&cond->queue_lock);
    BOOLEAN result = TRUE;

    if(cond->shift == CONDITION_NO_WAITERS)
        goto end;

    if((cond->queue & (1 << cond->shift--)) == CONDITION_USE_VARIABLE)
        WakeConditionVariable(&cond->variable);
    else
        result = ReleaseSemaphore(cond->handles[--cond->handle_count], 1, NULL);

    end:
        LeaveCriticalSection(&cond->queue_lock);
        return result ? thrd_success : thrd_error;
}

int cnd_broadcast(cnd_t* cond) {
    if(!cond)
        return thrd_error;
    
    EnterCriticalSection(&cond->queue_lock);
    BOOLEAN result = TRUE;

    if(cond->shift == CONDITION_NO_WAITERS)
        goto end;

    WakeAllConditionVariable(&cond->variable);

    if(cond->handle_count > 0) {
        BOOLEAN success = TRUE;
        while(cond->handle_count > 0) {
            if(!ReleaseSemaphore(cond->handles[--cond->handle_count], 1, NULL) && result)
                result = FALSE;
        }
    }

    cond->shift = CONDITION_NO_WAITERS;

    end:
        LeaveCriticalSection(&cond->queue_lock);
        return result ? thrd_success : thrd_error;
}

static int cnd_wait_ms(cnd_t* cond, mtx_t* mutex, DWORD ms) {
    if(!cond || !mutex)
        return thrd_error;
    
    EnterCriticalSection(&cond->queue_lock);
    if(cond->shift + 1 == 64) {
        LeaveCriticalSection(&cond->queue_lock);
        return thrd_error;
    }

    cond->shift++;

    switch(mutex->type) {
        case mtx_plain:
            cond->queue &= ~(1ull << cond->shift);
            LeaveCriticalSection(&cond->queue_lock);
            return SleepConditionVariableSRW(&cond->variable, &mutex->lock, ms, 0) ? thrd_success : thrd_error;
        case mtx_plain | mtx_recursive:
            cond->queue &= ~(1ull << cond->shift);
            LeaveCriticalSection(&cond->queue_lock);
            return SleepConditionVariableCS(&cond->variable, &mutex->section, ms) ? thrd_success : thrd_error;
        case mtx_timed:
        case mtx_timed | mtx_recursive:
            if(!mutex->handle) {
                LeaveCriticalSection(&cond->queue_lock);
                return thrd_error;
            }

            if(cond->handle_count == cond->handle_cap) {
                if(cond->handle_cap == 0) {
                    cond->handle_cap = 4;
                    cond->handles = malloc(sizeof(*cond->handles) * 4);
                    if(!cond->handles) {
                        LeaveCriticalSection(&cond->queue_lock);
                        return thrd_error;
                    }
                } else {
                    cond->handle_cap *= 1.5f;
                    void* buff = realloc(cond->handles, sizeof(*cond->handles) * cond->handle_cap);
                    if(!buff) {
                        LeaveCriticalSection(&cond->queue_lock);
                        return thrd_error;
                    }
                    cond->handles = buff;
                }
            }

            cond->queue |= (1ull << cond->shift);
            HANDLE sem = CreateSemaphore(NULL, 0, 1, NULL);
            cond->handles[cond->handle_count++] = sem;
            if(sem == NULL ||
                !(mutex->type  == mtx_timed ? ReleaseSemaphore(mutex->handle, 1, NULL) : 
                                              ReleaseMutex(mutex->handle))) 
            {
                cond->handle_count--;
                return thrd_error;
            }
            LeaveCriticalSection(&cond->queue_lock);
            int result;
            switch(WaitForSingleObject(sem, ms)) {
                case WAIT_ABANDONED:
                case WAIT_OBJECT_0:
                    result = thrd_success;
                    break;
                default:
                    result = thrd_error;
                    break;
            }
            CloseHandle(sem);
            WaitForSingleObject(&mutex->handle, INFINITE);
            return result;
        default:
            return thrd_error;
    }
}

int cnd_wait(cnd_t* cond, mtx_t* mutex) {
    return cnd_wait_ms(cond, mutex, INFINITE);
}

int cnd_timedwait(cnd_t* cond, mtx_t* mutex, const struct timespec* time_point) {
    if(!time_point)
        return thrd_error;

    struct timespec current;
    timespec_get(&current, TIME_UTC);
    timespec_subtract(&current, time_point, &current);

    if(current.tv_sec < 0)
        return thrd_timedout;

    DWORD ms = (DWORD) (current.tv_sec * 1000 + current.tv_nsec / 1000000);
    return cnd_wait_ms(cond, mutex, ms);
}

void cnd_destroy(cnd_t* cond) {
    if(!cond)
        return;

    DeleteCriticalSection(&cond->queue_lock);
    while(cond->handle_count > 0)
        CloseHandle(cond->handles[--cond->handle_count]);
    free(cond->handles);
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