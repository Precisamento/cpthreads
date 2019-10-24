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

#ifndef CP_THREADS_CP_THREADS_H
#define CP_THREADS_CP_THREADS_H

#if defined(_MSC_VER)

#include <time.h>

#include <windows.h>
#include <process.h>

// ============================================================================
// Threads
// ============================================================================

typedef int (*thrd_start_t)(void*);

typedef struct thrd_t {
    HANDLE handle;
    struct {
        thrd_start_t func;
        void* arg;
    } state;
} thrd_t;

enum {
    thrd_success,
    thrd_nomem,
    thrd_timedout,
    thrd_busy,
    thrd_error
};

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg);

static __inline int thrd_equal(thrd_t lhs, thrd_t rhs) {
    return lhs.handle == rhs.handle;
}

static __inline thrd_t thrd_current(void) {
    return (thrd_t){ GetCurrentThread() };
}

int thrd_sleep(const struct timespec* duration, struct timespec* remaining);

static __inline void thrd_yield(void) {
    SwitchToThread();
}

void thrd_exit(int res);

static __inline int thrd_detach(thrd_t thr) {
    return CloseHandle(thr.handle) ? thrd_success : thrd_error;
}

int thrd_join(thrd_t thr, int* res);

// ============================================================================
// Mutex
// ============================================================================

enum {
    mtx_plain = 1,
    mtx_recursive = 2,
    mtx_timed = 4
};

typedef struct mtx_t {
    union {
        HANDLE handle;
        CRITICAL_SECTION section;
        SRWLOCK lock;
    };
    int type;
} mtx_t;

// Where possible, use mtx_plain or mtx_plain | mtx_recursive.
// These are faster and less resource intensive than the alternatives.
int mtx_init(mtx_t* mutex, int type);
int mtx_lock(mtx_t* mutex);
int mtx_timedlock(mtx_t* mutex, const struct timespec* time_point);
int mtx_trylock(mtx_t* mutex);
int mtx_unlock(mtx_t* mutex);
void mtx_destroy(mtx_t* mutex);

typedef volatile LONG once_flag;

#define ONCE_FLAG_INIT 0

static __inline void call_once(once_flag* flag, void(*func)(void)) {
    if(*flag == 0 && InterlockedIncrement(flag)==1)
        func();
}

// ============================================================================
// Conditional Variables
// ============================================================================

typedef struct cnd_t {
    CRITICAL_SECTION queue_lock;
    CONDITION_VARIABLE variable;
    HANDLE* handles;
    unsigned long long queue;
    int shift;
    int handle_count;
    int handle_cap;
} cnd_t;

int cnd_init(cnd_t* cond);
int cnd_signal(cnd_t* cond);
int cnd_broadcast(cnd_t* cond);
int cnd_wait(cnd_t* cond, mtx_t* mutex);
int cnd_timedwait(cnd_t* cond, mtx_t* mutex, const struct timespec* time_point);
void cnd_destroy(cnd_t* cond);



// ============================================================================
// Thread Specific Storage
// ============================================================================

#define thread_local __declspec(thread)

#define TSS_DTOR_ITERATIONS 1

typedef void (*tss_dtor_t)(void*);

typedef DWORD tss_t;

int tss_create(tss_t* tss_key, tss_dtor_t destructor);
void tss_delete(tss_t tss_key);

static __inline void* tss_get(tss_t tss_key) {
    return TlsGetValue(tss_key);
}

static __inline int tss_set(tss_t tss_key, void* val) {
    if(TlsSetValue(tss_key, val))
        return thrd_success;
    return thrd_error;
}
 
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)

#include <threads.h>

#else
#error Must be able to use C99 or Windows Threads
#endif

#endif