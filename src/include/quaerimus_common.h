#ifndef QUAERIMUS_COMMON_H__
#define QUAERIMUS_COMMON_H__ 1

#include <stddef.h>

typedef struct {
  void *(*alloc)(void *userptr, size_t size);
  void *(*realloc)(void *userptr, void *old_ptr, size_t size);
  char *(*strndup)(void *userptr, const char *ptr, size_t len);
  void *(*memdup)(void *userptr, const void *ptr, size_t len);
  void (*free)(void *userptr, void *ptr);
} qury_allocator_t;

#endif /* QUAERIMUS_COMMON_H__ */
