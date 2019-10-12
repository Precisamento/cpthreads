#ifndef CP_THREADS_TEST_UTILS_H
#define CP_THREADS_TEST_UTILS_H

#include <time.h>

#define assert_thrd(expr) ck_assert((expr) == thrd_success)
#define ms2ts(ms) (struct timespec){ .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 }

#endif