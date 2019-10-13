#ifndef CP_THREADS_TEST_UTILS_H
#define CP_THREADS_TEST_UTILS_H

#include <check.h>
#include <time.h>
#include "../cpthreads.h"

#define assert_thrd(expr) ck_assert((expr) == thrd_success)
#define ms2ts(ms) (struct timespec){ .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 }

#define MUTEX_TYPES 4

static void initialize_mutexes(mtx_t mutexes[MUTEX_TYPES]) {
    assert_thrd(mtx_init(mutexes++, mtx_plain));
    assert_thrd(mtx_init(mutexes++, mtx_timed));
    assert_thrd(mtx_init(mutexes++, mtx_plain | mtx_recursive));
    assert_thrd(mtx_init(mutexes++, mtx_timed | mtx_recursive));
}

static void free_mutexes(mtx_t mutexes[MUTEX_TYPES]) {
    for(int i = 0; i < MUTEX_TYPES; i++)
        mtx_destroy(mutexes + i);
}

#endif