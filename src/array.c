#include "include/array.h"
#include "include/quaerimus_common.h"
#include <assert.h>
#include <memarena.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

bool array_init(array_t *array, size_t chunk_size,
                qury_allocator_t *mem_alloctor) {
  assert(array != NULL);
  memset(array, 0, sizeof(*array));
  array->allocator = mem_alloctor->init(0, NULL);
  array->chunk = chunk_size;
  array->mem = mem_alloctor;
  return true;
}

array_t *array_new(size_t chunk_size, qury_allocator_t *mem_alloctor) {
  array_t *array = NULL;
  void *allocator = mem_alloctor->init(sizeof(array_t), (void **)&array);
  if (allocator) {
    memset(array, 0, sizeof(*array));
    array->allocator = allocator;
    array->chunk = chunk_size;
    array->mem = mem_alloctor;
  }
  return array;
}

void array_destroy(array_t *array) {
  if (array && array->mem) {
    array->mem->free(array->allocator, array->ptrs);
    array->mem->destroy(array->allocator);
  }
}

void array_clear(array_t *array) {
  if (array) {
    array->used = 0;
  }
}

static int _grow_array(array_t *array, size_t size) {
  if (size == 0) {
    size = array->chunk;
  }
  uintptr_t *tmp =
      array->mem->realloc(array->allocator, array->ptrs,
                          sizeof(*array->ptrs) * (array->capacity + size));
  if (tmp) {
    memset((uint8_t *)tmp + (sizeof(*array->ptrs) * array->capacity), 0,
           sizeof(*array->ptrs) * size);
    array->ptrs = tmp;
    array->capacity += size;
    return 1;
  }
  return 0;
}

int array_push(array_t *array, uintptr_t item) {
  assert(array != NULL);
  if (array->used + 1 >= array->capacity) {
    if (!_grow_array(array, array->used + 1)) {
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

bool array_merge(array_t *dst, array_t *src) {
  if (!dst || !src) {
    return false;
  }
  if (dst->used + src->used >= dst->capacity) {
    if (!_grow_array(dst, src->used)) {
      return false;
    }
  }

  memcpy(&dst->ptrs[dst->used], &src->ptrs[0], sizeof(*src->ptrs) * src->used);
  dst->used += src->used;
  return true;
}
