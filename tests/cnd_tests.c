#include <check.h>

#include <stdbool.h>
#include <stdio.h>

#include "../cpthreads.h"
#include "test_utils.h"

static int test_num = 0;

static void cnd_test_start(void) {
    printf("Test number %d\n", test_num++);
}

typedef struct Lock {
    mtx_t* mutex;
    cnd_t* cond;
    bool done;
} Lock;

static int signaler(Lock* lock) {
    mtx_lock(lock->mutex);
    lock->done = true;
    printf("Signal: %d\n", cnd_signal(lock->cond));
    mtx_unlock(lock->mutex);
    return 1;
}

static int broadcaster(Lock* lock) {
    mtx_lock(lock->mutex);
    lock->done = true;
    printf("Broadcast: %d\n", cnd_broadcast(lock->cond));
    mtx_unlock(lock->mutex);
    return 1;
}

static int waiter(Lock* lock) {
    mtx_lock(lock->mutex);
    while(!lock->done) 
        printf("Wait: %d\n", cnd_wait(lock->cond, lock->mutex));
    mtx_unlock(lock->mutex);
    return 1;
}

START_TEST(waiter_waits_for_signaler) {
    mtx_t mutexes[MUTEX_TYPES];
    initialize_mutexes(mutexes);

    for(int i = 0; i < MUTEX_TYPES; i++) {
        thrd_t thread1, thread2;
        int result1, result2;
        cnd_t cond;
        assert_thrd(cnd_init(&cond));
        Lock lock = { mutexes + i, &cond, false };
        assert_thrd(thrd_create(&thread1, (int(*)(void*))waiter, (void*)&lock));
        thrd_sleep(&ms2ts(200), NULL);
        assert_thrd(thrd_create(&thread2, (int (*)(void*))signaler, (void*)&lock));
        thrd_join(thread1, &result1);
        thrd_join(thread2, &result2);
        ck_assert(result1 == 1);
        ck_assert(result2 == 1);
        cnd_destroy(&cond);
    }

    free_mutexes(mutexes);
}
END_TEST

START_TEST(single_waiter_waits_for_broadcaster) {
    mtx_t mutexes[MUTEX_TYPES];
    initialize_mutexes(mutexes);

    for(int i = 0; i < MUTEX_TYPES; i++) {
        thrd_t thread1, thread2;
        int result1, result2;
        cnd_t cond;
        assert_thrd(cnd_init(&cond));
        Lock lock = { mutexes + i, &cond, false };
        assert_thrd(thrd_create(&thread1, (int(*)(void*))waiter, (void*)&lock));
        thrd_sleep(&ms2ts(200), NULL);
        assert_thrd(thrd_create(&thread2, (int (*)(void*))broadcaster, (void*)&lock));
        thrd_join(thread1, &result1);
        thrd_join(thread2, &result2);
        ck_assert(result1 == 1);
        ck_assert(result2 == 1);
        cnd_destroy(&cond);
    }

    free_mutexes(mutexes);
}
END_TEST

START_TEST(multiple_waiters_wait_for_broadcaster) {
    mtx_t mutexes[MUTEX_TYPES];
    initialize_mutexes(mutexes);

    for(int i = 0; i < MUTEX_TYPES; i++) {
        thrd_t thread1, thread2, thread3;
        int result1, result2, result3;
        cnd_t cond;
        assert_thrd(cnd_init(&cond));
        Lock lock = { mutexes + i, &cond, false };
        assert_thrd(thrd_create(&thread1, (int(*)(void*))waiter, (void*)&lock));
        assert_thrd(thrd_create(&thread2, (int(*)(void*))waiter, (void*)&lock));
        thrd_sleep(&ms2ts(200), NULL);
        assert_thrd(thrd_create(&thread3, (int (*)(void*))broadcaster, (void*)&lock));
        thrd_join(thread1, &result1);
        thrd_join(thread2, &result2);
        thrd_join(thread3, &result3);
        ck_assert(result1 == 1);
        ck_assert(result2 == 1);
        ck_assert(result3 == 1);
        cnd_destroy(&cond);
    }

    free_mutexes(mutexes);

}
END_TEST

int main(void) {
    Suite* s = suite_create("Condition Tests");
    TCase* tc = tcase_create("Condition Tests");

    tcase_add_checked_fixture(tc, cnd_test_start, NULL);

    tcase_add_test(tc, waiter_waits_for_signaler);
    tcase_add_test(tc, single_waiter_waits_for_broadcaster);
    tcase_add_test(tc, multiple_waiters_wait_for_broadcaster);

    suite_add_tcase(s, tc);

    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}