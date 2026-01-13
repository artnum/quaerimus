#ifndef ARRAY_H__
#define ARRAY_H__ 1

#include <memarena.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct _array_t {
  mem_arena_t *arena;
  uintptr_t *ptrs;
  size_t capacity;
  size_t used;
  size_t chunk;
} array_t __attribute__((aligned(sizeof(max_align_t))));

bool array_init(array_t *array, size_t chunk_size);
array_t *array_new(size_t chunk_size);
void array_destroy(array_t *array);
void array_clear(array_t *array);
mem_arena_t *array_get_arena(array_t *array);

int array_push(array_t *array, uintptr_t item);
uintptr_t array_pop(array_t *array);
uintptr_t array_shift(array_t *array);
int array_unshift(array_t *array, uintptr_t item);
uintptr_t array_get(array_t *array, size_t idx);
int array_set(array_t *array, size_t idx, uintptr_t item);
uintptr_t array_remove(array_t *array, size_t idx);
#define array_size(array) ((array)->used)
#define array_foreach(array, index, value)                                     \
  for (index = 0; array && index < array_size(array) &&                        \
                  (value = array_get(array, index));                           \
       index++)

#endif /* ARRAY_H__ */
