/**
 * Unit tests for array_t (malloc + bump allocators).
 */
#include "../src/include/array.h"
#include "test_util.h"
#include <check.h>
#include <stdint.h>
#include <stdlib.h>

START_TEST(test_array_new_null_allocator) {
  ck_assert_ptr_null(array_new(8, NULL, NULL));
}
END_TEST

START_TEST(test_array_create_malloc) {
  array_t *a = array_new(8, &test_malloc_allocator, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_null(a->ptrs);
  ck_assert_uint_eq(a->capacity, 0);
  ck_assert_uint_eq(a->used, 0);
  ck_assert_uint_eq(a->chunk, 8);
  ck_assert(a->heap_owned);
  array_destroy(a);
}
END_TEST

START_TEST(test_array_init_embedded) {
  array_t a;
  ck_assert(array_init(&a, 4, &test_malloc_allocator, NULL));
  ck_assert(!a.heap_owned);
  ck_assert_int_eq(array_push(&a, 42), 1);
  ck_assert_uint_eq(array_size(&a), 1);
  ck_assert_uint_eq(array_get(&a, 0), 42);
  array_destroy(&a);
  /* embedded: array_t itself not freed; fields cleared */
  ck_assert_ptr_null(a.ptrs);
  ck_assert_ptr_null(a.mem);
}
END_TEST

START_TEST(test_array_push_pop) {
  array_t *a = array_new(2, &test_malloc_allocator, NULL);
  ck_assert_ptr_nonnull(a);

  for (int i = 0; i < 100; i++) {
    ck_assert_int_eq(array_push(a, (uintptr_t)(1000 + i)), 1);
  }
  ck_assert_uint_eq(array_size(a), 100);

  for (int i = 99; i >= 0; i--) {
    ck_assert_uint_eq(array_pop(a), (uintptr_t)(1000 + i));
  }
  ck_assert_uint_eq(array_size(a), 0);
  ck_assert_uint_eq(array_pop(a), 0);

  array_destroy(a);
}
END_TEST

START_TEST(test_array_shift_unshift) {
  array_t *a = array_new(4, &test_malloc_allocator, NULL);
  ck_assert_ptr_nonnull(a);

  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_push(a, (uintptr_t)(5000 + i)), 1);
  }
  for (int i = 0; i < 10; i++) {
    ck_assert_uint_eq(array_shift(a), (uintptr_t)(5000 + i));
  }
  ck_assert_uint_eq(array_size(a), 0);
  ck_assert_uint_eq(array_shift(a), 0);

  ck_assert_int_eq(array_unshift(a, 3), 1);
  ck_assert_int_eq(array_unshift(a, 2), 1);
  ck_assert_int_eq(array_unshift(a, 1), 1);
  ck_assert_uint_eq(array_size(a), 3);
  ck_assert_uint_eq(array_get(a, 0), 1);
  ck_assert_uint_eq(array_get(a, 1), 2);
  ck_assert_uint_eq(array_get(a, 2), 3);

  array_destroy(a);
}
END_TEST

START_TEST(test_array_set_get_remove) {
  array_t *a = array_new(4, &test_malloc_allocator, NULL);
  ck_assert_ptr_nonnull(a);

  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(array_set(a, (size_t)i, (uintptr_t)((i + 1) * 3)), 1);
  }
  ck_assert_uint_eq(a->used, 10);
  for (int i = 0; i < 10; i++) {
    ck_assert_uint_eq(array_get(a, (size_t)i), (uintptr_t)((i + 1) * 3));
  }

  /* sparse set */
  array_destroy(a);
  a = array_new(4, &test_malloc_allocator, NULL);
  for (int i = 0; i < 50; i++) {
    if (i % 3) {
      ck_assert_int_eq(array_set(a, (size_t)i, (uintptr_t)((i + 1) * 3)), 1);
    }
  }
  ck_assert_uint_eq(a->used, 50);
  for (int i = 0; i < 50; i++) {
    if (i % 3) {
      ck_assert_uint_eq(array_get(a, (size_t)i), (uintptr_t)((i + 1) * 3));
    } else {
      ck_assert_uint_eq(array_get(a, (size_t)i), 0);
    }
  }

  /* remove middle */
  array_clear(a);
  ck_assert_int_eq(array_push(a, 10), 1);
  ck_assert_int_eq(array_push(a, 20), 1);
  ck_assert_int_eq(array_push(a, 30), 1);
  ck_assert_uint_eq(array_remove(a, 1), 20);
  ck_assert_uint_eq(array_size(a), 2);
  ck_assert_uint_eq(array_get(a, 0), 10);
  ck_assert_uint_eq(array_get(a, 1), 30);
  ck_assert_uint_eq(array_remove(a, 99), 0);

  array_destroy(a);
}
END_TEST

START_TEST(test_array_merge_clear_foreach) {
  array_t *a = array_new(4, &test_malloc_allocator, NULL);
  array_t *b = array_new(4, &test_malloc_allocator, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_nonnull(b);

  ck_assert_int_eq(array_push(a, 1), 1);
  ck_assert_int_eq(array_push(a, 2), 1);
  ck_assert_int_eq(array_push(b, 3), 1);
  ck_assert_int_eq(array_push(b, 4), 1);
  ck_assert(array_merge(a, b));
  ck_assert_uint_eq(array_size(a), 4);
  ck_assert_uint_eq(array_get(a, 0), 1);
  ck_assert_uint_eq(array_get(a, 3), 4);

  size_t index;
  uintptr_t value;
  size_t sum = 0;
  array_foreach(a, index, value) { sum += value; }
  ck_assert_uint_eq(sum, 10);

  /* foreach visits zero slots */
  array_clear(a);
  ck_assert_int_eq(array_push(a, 0), 1);
  ck_assert_int_eq(array_push(a, 7), 1);
  size_t visits = 0;
  array_foreach(a, index, value) { visits++; }
  ck_assert_uint_eq(visits, 2);

  array_destroy(a);
  array_destroy(b);
}
END_TEST

START_TEST(test_array_bump_allocator) {
  test_bump_t bump;
  ck_assert(test_bump_init(&bump, 64 * 1024));

  array_t *a = array_new(8, &test_bump_allocator, &bump);
  ck_assert_ptr_nonnull(a);

  for (int i = 0; i < 200; i++) {
    ck_assert_int_eq(array_push(a, (uintptr_t)i), 1);
  }
  ck_assert_uint_eq(array_size(a), 200);
  for (int i = 0; i < 200; i++) {
    ck_assert_uint_eq(array_get(a, (size_t)i), (uintptr_t)i);
  }

  /* destroy must not crash when free is NULL (region still owns memory) */
  array_destroy(a);

  /* reset reclaims the region for reuse without per-block free */
  test_bump_reset(&bump);
  a = array_new(8, &test_bump_allocator, &bump);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(array_push(a, 99), 1);
  ck_assert_uint_eq(array_get(a, 0), 99);
  array_destroy(a);

  test_bump_destroy(&bump);
}
END_TEST

static Suite *array_suite(void) {
  Suite *s = suite_create("array");

  TCase *tc = tcase_create("core");
  tcase_add_test(tc, test_array_new_null_allocator);
  tcase_add_test(tc, test_array_create_malloc);
  tcase_add_test(tc, test_array_init_embedded);
  tcase_add_test(tc, test_array_push_pop);
  tcase_add_test(tc, test_array_shift_unshift);
  tcase_add_test(tc, test_array_set_get_remove);
  tcase_add_test(tc, test_array_merge_clear_foreach);
  tcase_add_test(tc, test_array_bump_allocator);
  suite_add_tcase(s, tc);

  return s;
}

int main(void) {
  Suite *s = array_suite();
  SRunner *sr = srunner_create(s);
  srunner_set_fork_status(sr, CK_NOFORK);
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
