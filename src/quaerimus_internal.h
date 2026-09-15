#ifndef QUAERIMUS_INTERNAL_H__
#define QUAERIMUS_INTERNAL_H__

#include "include/quaerimus_common.h"

/* Shared allocator, set by qury_init(). Not part of the public API. */
extern qury_allocator_t *MemoryAllocator;

#endif /* QUAERIMUS_INTERNAL_H__ */
