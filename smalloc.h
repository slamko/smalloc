#ifndef SLAB_ALLOC_H
#define SLAB_ALLOC_H

#include <stdint.h>
#include <stddef.h>

struct slab;

struct slab_cache;

typedef struct slab slab_t;

typedef struct slab_cache slab_cache_t;

struct slab_cache *slab_cache_create(size_t size);

struct slab_cache *slab_cache_create_capacity(size_t size, size_t slab_capa);

void slab_cache_destroy(struct slab_cache *cache);

void *slab_alloc_from_cache(struct slab_cache *cache);

struct slab_cache *slab_cache_create_align(size_t size, size_t alignment,
                                           size_t slab_capacity);

void slab_free(struct slab_cache *cache, void *obj);

int slab_alloc_init(uintptr_t base);

#endif
