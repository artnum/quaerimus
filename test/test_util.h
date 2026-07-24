#ifndef TEST_UTIL_H__
#define TEST_UTIL_H__

#include "../src/include/quaerimus_common.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(x) (void)(x)

/* --- libc-backed allocator (free is non-NULL) --- */

static void *test_malloc_alloc(void *uptr, size_t size) {
  UNUSED(uptr);
  return malloc(size);
}

static void *test_malloc_realloc(void *uptr, void *ptr, size_t size) {
  UNUSED(uptr);
  return realloc(ptr, size);
}

static void test_malloc_free(void *uptr, void *ptr) {
  UNUSED(uptr);
  free(ptr);
}

static char *test_malloc_strndup(void *uptr, const char *ptr, size_t len) {
  UNUSED(uptr);
  return strndup(ptr, len);
}

static void *test_malloc_memdup(void *uptr, const void *ptr, size_t len) {
  UNUSED(uptr);
  void *tmp = malloc(len);
  if (tmp && len) {
    memcpy(tmp, ptr, len);
  }
  return tmp;
}

static qury_allocator_t test_malloc_allocator = {
    .alloc = test_malloc_alloc,
    .realloc = test_malloc_realloc,
    .free = test_malloc_free,
    .strndup = test_malloc_strndup,
    .memdup = test_malloc_memdup,
};

/* --- bump allocator (free is NULL; region owns all blocks) --- */

typedef struct {
  uint8_t *base;
  size_t capacity;
  size_t used;
} test_bump_t;

static size_t test_bump_align(size_t n) {
  return (n + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);
}

static int test_bump_init(test_bump_t *b, size_t capacity) {
  memset(b, 0, sizeof(*b));
  b->base = (uint8_t *)malloc(capacity);
  if (!b->base) {
    return 0;
  }
  b->capacity = capacity;
  b->used = 0;
  return 1;
}

static void test_bump_reset(test_bump_t *b) {
  if (b) {
    b->used = 0;
  }
}

static void test_bump_destroy(test_bump_t *b) {
  free(b->base);
  memset(b, 0, sizeof(*b));
}

/*
 * Each allocation is preceded by a size_t payload length so realloc can
 * copy (array growth needs real realloc semantics; the old block is still
 * abandoned in the region — that is normal for bump).
 */
static void *test_bump_alloc(void *uptr, size_t size) {
  test_bump_t *b = (test_bump_t *)uptr;
  size_t payload = size ? size : 1;
  size_t n = test_bump_align(sizeof(size_t) + payload);
  if (b->used + n > b->capacity) {
    return NULL;
  }
  size_t *hdr = (size_t *)(b->base + b->used);
  *hdr = payload;
  b->used += n;
  return hdr + 1;
}

static void *test_bump_realloc(void *uptr, void *old, size_t size) {
  if (!old) {
    return test_bump_alloc(uptr, size);
  }
  size_t old_size = ((size_t *)old)[-1];
  void *neu = test_bump_alloc(uptr, size);
  if (!neu) {
    return NULL;
  }
  size_t copy = old_size < size ? old_size : size;
  if (copy) {
    memcpy(neu, old, copy);
  }
  return neu;
}

static char *test_bump_strndup(void *uptr, const char *ptr, size_t len) {
  char *out = (char *)test_bump_alloc(uptr, len + 1);
  if (!out) {
    return NULL;
  }
  if (len) {
    memcpy(out, ptr, len);
  }
  out[len] = '\0';
  return out;
}

static void *test_bump_memdup(void *uptr, const void *ptr, size_t len) {
  void *out = test_bump_alloc(uptr, len ? len : 1);
  if (!out) {
    return NULL;
  }
  if (len) {
    memcpy(out, ptr, len);
  }
  return out;
}

static qury_allocator_t test_bump_allocator = {
    .alloc = test_bump_alloc,
    .realloc = test_bump_realloc,
    .free = NULL, /* intentional: bump has no per-block free */
    .strndup = test_bump_strndup,
    .memdup = test_bump_memdup,
};

#endif /* TEST_UTIL_H__ */
