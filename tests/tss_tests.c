#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "../cpthreads.h"
#include "test_utils.h"

static int test_num = 0;
static volatile LONG destructions = 0;

static tss_t key;

static void value_destructor(void* value) {
    if(value) {
        InterlockedIncrement(&destructions);
        free(value);
    }
}

static void tss_test_setup(void) {
    tss_create(&key, value_destructor);
}

static void tss_test_teardown(void) {
    tss_delete(key);
}

static void tss_test_start(void) {
    printf("Test number %d\n", test_num++);
}

static void thread_allocate(void* arg) {

}

START_TEST(tss_create_generates_unique_key) {
    tss_t temp;
    assert_thrd(tss_create(&temp, NULL));
    ck_assert(temp != key);
    tss_delete(temp);
}
END_TEST

START_TEST(tss_value_set_and_get_main_thread) {
    void* mem = malloc(1);
    assert_thrd(tss_set(key, mem));
    ck_assert(tss_get(key) == mem);
    free(mem);
    tss_set(key, NULL);
}
END_TEST

static int set_and_get(void* arg) {
    void* mem = malloc(1);
    if(tss_set(key, mem) != thrd_success) 
        return thrd_error;

    if(tss_get(key) != mem)
        return thrd_error;

    if(tss_set(key, NULL) != thrd_success) 
        return thrd_error;

    free(mem);
    return thrd_success;
}

START_TEST(tss_value_set_and_get_seperate_thread) {
    thrd_t thread;
    thrd_create(&thread, set_and_get, NULL);
    int result;
    thrd_join(thread, &result);
    ck_assert(result == thrd_success);
}
END_TEST

static int float_memory(void* arg) {
    void* mem = malloc(1);
    if(tss_set(key, mem) != thrd_success)
        return thrd_error;
    return thrd_success;
}

static int float_memory_thrd_exit(void* arg) {
    void* mem = malloc(1);
    if(tss_set(key, mem) != thrd_success)
        thrd_exit(thrd_error);
    thrd_exit(thrd_success);
    return 0;
}

START_TEST(thread_return_tss_value_freed_by_dtor) {
    thrd_t thread;
    DWORD before = destructions;
    thrd_create(&thread, float_memory, NULL);
    int result;
    thrd_join(thread, &result);
    ck_assert(result == thrd_success);
    ck_assert(destructions == before + 1);
}
END_TEST

START_TEST(thread_exit_tss_value_freed_by_dtor) {
    thrd_t thread;
    DWORD before = destructions;
    thrd_create(&thread, float_memory_thrd_exit, NULL);
    int result;
    thrd_join(thread, &result);
    ck_assert(result == thrd_success);
    ck_assert(destructions == before + 1);
}
END_TEST

static int nop(void* arg) { return 1; }

START_TEST(thread_without_tss_set_does_not_trigger_dtor) {
    thrd_t thread;
    DWORD before = destructions;
    thrd_create(&thread, nop, NULL);
    thrd_detach(thread);
    ck_assert(destructions == before);
}
END_TEST

static int compare_mem(void* arg) {
    void* mem = malloc(1);
    if(tss_set(key, mem) != thrd_success)
        return thrd_error;

    if(tss_get(key) == arg)
        return thrd_error;

    return thrd_success;
}

START_TEST(tss_not_shared_between_threads) {
    void* mem = malloc(1);
    tss_set(key, mem);

    thrd_t thread;
    int result;
    thrd_create(&thread, compare_mem, mem);
    thrd_join(thread, &result);
    ck_assert(result == thrd_success);
    ck_assert(tss_get(key) == mem);
    free(mem);
    tss_set(key, NULL);
}
END_TEST

thread_local int thread_local_value;

static int alter_thread_local_value(void* arg) {
    thread_local_value = *(int*)arg;
    return thread_local_value;
}

START_TEST(thread_local_altered_by_two_threads_has_seperate_values) {
    thread_local_value = 0;
    thrd_t thread;
    thrd_create(&thread, alter_thread_local_value, &(int){5});
    int result;
    thrd_join(thread, &result);
    ck_assert(result == 5);
    ck_assert(thread_local_value == 0);
}
END_TEST

int main(void) {
    Suite* s = suite_create("TSS Tests");
    TCase* tc = tcase_create("TSS Tests");

    tcase_add_unchecked_fixture(tc, tss_test_setup, tss_test_teardown);
    tcase_add_checked_fixture(tc, tss_test_start, NULL);

    tcase_add_test(tc, tss_create_generates_unique_key);
    tcase_add_test(tc, tss_value_set_and_get_main_thread);
    tcase_add_test(tc, tss_value_set_and_get_seperate_thread);
    tcase_add_test(tc, thread_return_tss_value_freed_by_dtor);
    tcase_add_test(tc, thread_exit_tss_value_freed_by_dtor);
    tcase_add_test(tc, thread_without_tss_set_does_not_trigger_dtor);
    tcase_add_test(tc, tss_not_shared_between_threads);
    tcase_add_test(tc, thread_local_altered_by_two_threads_has_seperate_values);

    suite_add_tcase(s, tc);

    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}