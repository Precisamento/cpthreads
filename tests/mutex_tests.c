#include <check.h>
#include <stdio.h>

#include "../cpthreads.h"
#include "test_utils.h"

static int test_num = 0;

static void mutex_test_start(void) {
    printf("Test number %d\n", test_num++);
}

START_TEST(mtx_init_plain) {
    mtx_t mutex;
    assert_thrd(mtx_init(&mutex, mtx_plain));
    mtx_destroy(&mutex);
}
END_TEST

START_TEST(mtx_init_timed) {
    mtx_t mutex;
    assert_thrd(mtx_init(&mutex, mtx_timed));
    mtx_destroy(&mutex);
}
END_TEST

START_TEST(mtx_init_plain_recursive) {
    mtx_t mutex;
    assert_thrd(mtx_init(&mutex, mtx_plain | mtx_recursive));
    mtx_destroy(&mutex);
}
END_TEST

START_TEST(mtx_init_timed_recursive) {
    mtx_t mutex;
    assert_thrd(mtx_init(&mutex, mtx_timed | mtx_recursive));
    mtx_destroy(&mutex);
}
END_TEST

static int single_lock_test(void* arg) {
    mtx_t* mutex = arg;
    if(mtx_lock(mutex) != thrd_success)
        return 0;
    thrd_sleep(&ms2ts(1000), NULL);
    if(mtx_unlock(mutex) != thrd_success)
        return 0;
    return 1;
}

static int double_lock_test(void* arg) {
    mtx_t* mutex = arg;
    if(mtx_lock(mutex) != thrd_success)
        return 0;
    if(mtx_lock(mutex) != thrd_success)
        return 0;
    thrd_sleep(&ms2ts(1000), NULL);
    if(mtx_unlock(mutex) != thrd_success)
        return 0;
    if(mtx_unlock(mutex) != thrd_success)
        return 0;
    return 1;
}

typedef struct {
    int sleep_ms;
    int time_out_ms;
    mtx_t* mutex;
} TimeLock;

static int time_lock_test(void* arg) {
    TimeLock* lock = arg;
    DWORD id = GetCurrentThreadId();
    int result;

    struct timespec absolute;
    timespec_get(&absolute, TIME_UTC);
    absolute.tv_sec += lock->time_out_ms / 1000;
    absolute.tv_nsec += (lock->time_out_ms % 1000) * 1000000;

    if((result = mtx_timedlock(lock->mutex, &absolute)) != thrd_success)
        return result;
    result = thrd_sleep(&ms2ts(lock->sleep_ms), NULL);
    mtx_unlock(lock->mutex);
    return result;
}

static int try_lock_test(void* arg) {
    mtx_t* mutex = arg;
    int result;
    if((result = mtx_trylock(mutex)) != thrd_success)
        return result;
    result = thrd_sleep(&ms2ts(1000), NULL);
    mtx_unlock(mutex);
    return result;
}

START_TEST(mtx_simple_lock_non_blocking) {
    mtx_t mutexes[MUTEX_TYPES];
    initialize_mutexes(mutexes);
    for(int i = 0; i < MUTEX_TYPES; i++) {
        thrd_t thread1, thread2;
        int result1, result2;
        assert_thrd(thrd_create(&thread1, single_lock_test, mutexes + i));
        assert_thrd(thrd_create(&thread2, single_lock_test, mutexes + i));
        assert_thrd(thrd_join(thread1, &result1));
        assert_thrd(thrd_join(thread2, &result2));
        ck_assert(result1 == 1);
        ck_assert(result2 == 1);
    }
    free_mutexes(mutexes);
}
END_TEST

START_TEST(mtx_double_lock_non_blocking) {
    mtx_t mutexes[2];
    assert_thrd(mtx_init(mutexes, mtx_plain | mtx_recursive));
    assert_thrd(mtx_init(mutexes + 1, mtx_timed | mtx_recursive));

    for(int i = 0; i < 2; i++) {
        thrd_t thread1, thread2;
        int result1, result2;
        assert_thrd(thrd_create(&thread1, double_lock_test, mutexes + i));
        assert_thrd(thrd_create(&thread2, double_lock_test, mutexes + i));
        assert_thrd(thrd_join(thread1, &result1));
        assert_thrd(thrd_join(thread2, &result2));
        ck_assert(result1 == 1);
        ck_assert(result2 == 1);
    }

    mtx_destroy(mutexes);
    mtx_destroy(mutexes + 1);    
}
END_TEST

START_TEST(mtx_timed_lock_second_times_out) {
    mtx_t mutexes[2];
    assert_thrd(mtx_init(mutexes, mtx_timed));
    assert_thrd(mtx_init(mutexes + 1, mtx_timed | mtx_recursive));

    for(int i = 0; i < 2; i++) {
        printf("Iteration %d\n", i);
        thrd_t thread1, thread2;
        int result1, result2;
        assert_thrd(thrd_create(&thread1, time_lock_test, &(TimeLock){ 2000, 0, mutexes + i }));
        thrd_sleep(&ms2ts(50), NULL);
        assert_thrd(thrd_create(&thread2, time_lock_test, &(TimeLock){ 100, 50, mutexes + i }));
        assert_thrd(thrd_join(thread1, &result1));
        assert_thrd(thrd_join(thread2, &result2));
        printf("Result: %d\n", result1);
        ck_assert(result1 == thrd_success);
        ck_assert(result2 == thrd_timedout);
    }

    mtx_destroy(mutexes);
    mtx_destroy(mutexes + 1);   
}
END_TEST

START_TEST(mtx_try_lock_second_fails) {
    mtx_t mutexes[MUTEX_TYPES];
    initialize_mutexes(mutexes);
    for(int i = 0; i < MUTEX_TYPES; i++) {
        thrd_t thread1, thread2;
        int result1, result2;
        assert_thrd(thrd_create(&thread1, try_lock_test, mutexes + i));
        thrd_sleep(&ms2ts(50), NULL);
        assert_thrd(thrd_create(&thread2, try_lock_test, mutexes + i));
        assert_thrd(thrd_join(thread1, &result1));
        assert_thrd(thrd_join(thread2, &result2));
        ck_assert(result1 == thrd_success);
        ck_assert(result2 == thrd_busy);
    }
    free_mutexes(mutexes);
}
END_TEST

int main(void) {
    Suite* s = suite_create("Mutex Tests");
    TCase* tc = tcase_create("Mutex Tests");

    tcase_add_checked_fixture(tc, mutex_test_start, NULL);

    tcase_add_test(tc, mtx_init_plain);
    tcase_add_test(tc, mtx_init_timed);
    tcase_add_test(tc, mtx_init_plain_recursive);
    tcase_add_test(tc, mtx_init_timed_recursive);
    tcase_add_test(tc, mtx_simple_lock_non_blocking);
    tcase_add_test(tc, mtx_double_lock_non_blocking);
    tcase_add_test(tc, mtx_timed_lock_second_times_out);
    tcase_add_test(tc, mtx_try_lock_second_fails);

    suite_add_tcase(s, tc);

    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}