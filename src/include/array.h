#ifndef ARRAY_H__
#define ARRAY_H__ 1

#include "quaerimus_common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct _array_t {
  void *allocator;
  uintptr_t *ptrs;
  size_t capacity;
  size_t used;
  size_t chunk;
  qury_allocator_t *mem;
} array_t __attribute__((aligned(sizeof(max_align_t))));

array_t *array_new(size_t chunk_size, qury_allocator_t *mem_alloctor);
bool array_init(array_t *array, size_t chunk_size,
                qury_allocator_t *mem_alloctor);
void array_destroy(array_t *array);
void array_clear(array_t *array);

int array_push(array_t *array, uintptr_t item);
uintptr_t array_pop(array_t *array);
uintptr_t array_shift(array_t *array);
int array_unshift(array_t *array, uintptr_t item);
uintptr_t array_get(array_t *array, size_t idx);
int array_set(array_t *array, size_t idx, uintptr_t item);
uintptr_t array_remove(array_t *array, size_t idx);
bool array_merge(array_t *dst, array_t *src);
#define array_size(array) ((array)->used)
#define array_foreach(array, index, value)                                     \
  for (index = 0; array && index < array_size(array) &&                        \
                  (value = array_get(array, index));                           \
       index++)

#endif /* ARRAY_H__ */
