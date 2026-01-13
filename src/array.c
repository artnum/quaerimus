#include "include/array.h"
#include <assert.h>
#include <memarena.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

bool array_init(array_t *array, size_t chunk_size) {
  assert(array != NULL);
  memset(array, 0, sizeof(*array));
  array->arena = mem_arena_new(chunk_size * sizeof(uintptr_t));
  if (!array->arena) {
    return false;
  }
  array->chunk = chunk_size;
  return true;
}

array_t *array_new(size_t chunk_size) {
  array_t *array = NULL;
  chunk_size *= sizeof(uintptr_t);
  mem_arena_t *arena =
      mem_arena_new_embed(chunk_size, sizeof(*array), (void **)&array);
  if (arena) {
    memset(array, 0, sizeof(*array));
    array->arena = arena;
    array->chunk = chunk_size;
  }
  return array;
}

mem_arena_t *array_get_arena(array_t *array) {
  if (!array) {
    return NULL;
  }
  return array->arena;
}

void array_destroy(array_t *array) {
  if (array) {
    mem_arena_destroy(array->arena);
  }
}

void array_clear(array_t *array) {
  if (array) {
    mem_arena_reset(array->arena);
    array->used = 0;
  }
}

static int _grow_array(array_t *array, size_t size) {
  if (size == 0) {
    size = array->chunk;
  }
  uintptr_t *tmp = mem_realloc(array->arena, array->ptrs,
                               sizeof(*array->ptrs) * (array->capacity + size));
  if (tmp) {
    array->ptrs = tmp;
    array->capacity += size;
    return 1;
  }
  return 0;
}

int array_push(array_t *array, uintptr_t item) {
  assert(array != NULL);
  if (array->used + 1 >= array->capacity) {
    if (!_grow_array(array, 0)) {
      return 0;
    }
  }
  array->ptrs[array->used++] = item;
  return 1;
}

uintptr_t array_pop(array_t *array) {
  assert(array != NULL);
  if (array->used == 0) {
    return 0;
  }
  return array->ptrs[--array->used];
}

uintptr_t array_shift(array_t *array) {
  assert(array != NULL);
  if (array->used == 0) {
    return 0;
  }
  uintptr_t item = array->ptrs[0];
  memmove(&array->ptrs[0], &array->ptrs[1], array->used * sizeof(uintptr_t));
  return item;
}

int array_unshift(array_t *array, uintptr_t item) {
  assert(array != NULL);
  if (array->used + 1 >= array->capacity) {
    if (!_grow_array(array, 0)) {
      return 0;
    }
  }
  memmove(&array->ptrs[0], &array->ptrs[1], array->used * sizeof(uintptr_t));
  array->ptrs[0] = item;
  array->used++;
  return 1;
}

uintptr_t array_get(array_t *array, size_t idx) {
  if (idx >= array->used) {
    return 0;
  }
  return array->ptrs[idx];
}

int array_set(array_t *array, size_t idx, uintptr_t item) {
  if (idx >= array->capacity) {
    if (!_grow_array(array, idx - array->capacity + 1)) {
      return 0;
    }
  }
  array->ptrs[idx] = item;
  if (idx > array->used) {
    array->used = idx + 1;
  }
  return 1;
}

uintptr_t array_remove(array_t *array, size_t idx) {
  if (idx >= array->used) {
    return 0;
  }
  uintptr_t item = array->ptrs[idx];
  memmove(&array->ptrs[idx], &array->ptrs[idx + 1],
          sizeof(uintptr_t) * (array->used - idx + 1));
  array->ptrs[idx] = item;
  return 1;
}
