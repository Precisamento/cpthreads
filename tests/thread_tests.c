#include <check.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#include "../cpthreads.h"
#include "test_utils.h"

static int get_thread_id_windows(void* arg) {
    return GetCurrentThreadId();
}

static int nop(void* arg) {
    return 0;
}

static int exit1(void* arg) {
    return 1;
}

static int exit2(void* arg) {
    thrd_exit(2);
    return 3;
}

static int exit3(void* arg) {
    return *(int*)arg;
}

static int test_sleep(void* arg) {
    thrd_sleep(&ms2ts(*(int*)arg), NULL);
    return 0;
}

static int test_num = 0;

static void thread_test_start(void) {
    printf("Test number %d\n", test_num++);
}

START_TEST(thrd_create_spawns_new_thread) {
    thrd_t thread;
    assert_thrd(thrd_create(&thread, nop, NULL));
    assert_thrd(thrd_join(thread, NULL));
}
END_TEST

START_TEST(thrd_function_returns_value) {
    thrd_t thread;
    int result;
    assert_thrd(thrd_create(&thread, exit1, NULL));
    assert_thrd(thrd_join(thread, &result));
    ck_assert(result == 1);
} 
END_TEST

START_TEST(thrd_exit_returns_exit_code) {
    thrd_t thread;
    int result;
    assert_thrd(thrd_create(&thread, exit2, NULL));
    assert_thrd(thrd_join(thread, &result));
    ck_assert(result == 2);
}
END_TEST

START_TEST(thrd_function_gets_argument) {
    thrd_t thread;
    int arg = 3;
    int result;
    assert_thrd(thrd_create(&thread, exit3, (void*)&arg));
    assert_thrd(thrd_join(thread, &result));
    ck_assert(result == arg);
}
END_TEST

START_TEST(thrd_equal_same_threads_true) {
    thrd_t thread1, thread2;
    assert_thrd(thrd_create(&thread1, nop, NULL));
    thread2 = thread1;
    ck_assert(thrd_equal(thread1, thread2));
    assert_thrd(thrd_join(thread1, NULL));
}
END_TEST

START_TEST(thrd_equal_different_threads_false) {
    thrd_t thread1, thread2;
    assert_thrd(thrd_create(&thread1, nop, NULL));
    assert_thrd(thrd_create(&thread2, nop, NULL));
    ck_assert(!thrd_equal(thread1, thread2));
    assert_thrd(thrd_join(thread1, NULL));
    assert_thrd(thrd_join(thread2, NULL));
}
END_TEST

START_TEST(thrd_sleep_2000_ms_waits_approximately_two_seconds) {
    clock_t time = clock();
    ck_assert(thrd_sleep(&ms2ts(2000), NULL) == 0);
    time = clock() - time;
    double seconds = fabs((((double)time) / CLOCKS_PER_SEC) - 2.0);
    ck_assert(seconds < 0.5);
}
END_TEST

START_TEST(thrd_detach_doesnt_throw) {
    thrd_t thread;
    clock_t time = clock();
    assert_thrd(thrd_create(&thread, test_sleep, &(int){10000}));
    assert_thrd(thrd_detach(thread));
    time = clock() - time;
    double seconds = ((double)time) / CLOCKS_PER_SEC;
    printf("%lg\n", seconds);
    ck_assert(seconds < 0.5);
}
END_TEST

int main(void) {
    Suite* s = suite_create("Thread Tests");
    TCase* tc = tcase_create("Thread Tests");

    tcase_add_checked_fixture(tc, thread_test_start, NULL);

    tcase_add_test(tc, thrd_create_spawns_new_thread);
    tcase_add_test(tc, thrd_function_returns_value);
    tcase_add_test(tc, thrd_exit_returns_exit_code);
    tcase_add_test(tc, thrd_function_gets_argument);
    tcase_add_test(tc, thrd_equal_same_threads_true);
    tcase_add_test(tc, thrd_equal_different_threads_false);
    tcase_add_test(tc, thrd_sleep_2000_ms_waits_approximately_two_seconds);
    tcase_add_test(tc, thrd_detach_doesnt_throw);

    suite_add_tcase(s, tc);

    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}