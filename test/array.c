#include "../src/include/array.h"
#include <check.h>
#include <stdint.h>
#include <stdlib.h>

START_TEST(test_array_create) {
  array_t *a = array_new(10);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_null(a->ptrs);
  ck_assert_int_eq(a->capacity, 0);
  ck_assert_int_eq(a->chunk, 10 * sizeof(uintptr_t));
  ck_assert_int_eq(a->used, 0);
  array_destroy(a);
}
END_TEST

START_TEST(test_array_fifo) {
  /* make array requiring realloc each insertion */
  array_t *a = array_new(sizeof(uintptr_t));
  ck_assert_ptr_nonnull(a);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_push(a, 5000 + i), 1);
  }
  ck_assert_int_eq(a->used, 10);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_shift(a), 5000 + i);
  }
  array_destroy(a);
}
END_TEST

START_TEST(test_array_lifo) {
  /* make array requiring realloc each insertion */
  array_t *a = array_new(sizeof(uintptr_t));
  ck_assert_ptr_nonnull(a);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_push(a, 5000 + i), 1);
  }
  ck_assert_int_eq(a->used, 10);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_pop(a), 5000 + 10 - 1);
  }
  array_destroy(a);
}
END_TEST
#include <stdio.h>
START_TEST(test_array_set) {
  array_t *a = array_new(sizeof(uintptr_t));
  ck_assert_ptr_nonnull(a);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_set(a, i, (i + 1) * 3), 1);
  }
  ck_assert_int_eq(a->used, 10);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_get(a, i), (i + 1) * 3);
  }
  array_destroy(a);

  a = array_new(sizeof(uintptr_t));
  ck_assert_ptr_nonnull(a);
  for (int i = 0; i < 50; i++) {
    if (i % 3) {
      ck_assert_int_eq(array_set(a, i, (i + 1) * 3), 1);
    }
  }
  /* set moves used to that last index+1, so 50 item is 49, 49 + 1 == 50 */
  ck_assert_int_eq(a->used, 50);
  for (int i = 0; i < 50; i++) {
    if (i % 3) {
      fprintf(stderr, "%d %ld \n", i, array_get(a, i));
      ck_assert_int_eq(array_get(a, i), (i + 1) * 3);
    } else {
      ck_assert_int_eq(array_get(a, i), 0);
    }
  }
  array_destroy(a);
}
END_TEST

Suite *test_suite_array(void) {
  Suite *s;
  s = suite_create("array.c test");

  TCase *tc_create = tcase_create("Create");
  tcase_add_test(tc_create, test_array_create);
  suite_add_tcase(s, tc_create);

  TCase *tc_fifo = tcase_create("FIFO");
  tcase_add_test(tc_fifo, test_array_fifo);
  suite_add_tcase(s, tc_fifo);

  TCase *tc_lifo = tcase_create("LIFO");
  tcase_add_test(tc_lifo, test_array_fifo);
  suite_add_tcase(s, tc_lifo);

  TCase *tc_set = tcase_create("SET");
  tcase_add_test(tc_lifo, test_array_set);
  suite_add_tcase(s, tc_set);

  return s;
}

int main(void) {
  int failed = 0;
  Suite *s;
  SRunner *sr;

  s = test_suite_array();
  sr = srunner_create(s);
  srunner_set_fork_status(sr, CK_NOFORK);
  srunner_run_all(sr, CK_VERBOSE);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
