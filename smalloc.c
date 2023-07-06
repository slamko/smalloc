#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

struct slab_chunk {
    struct slab_chunk *next;
    struct slab_chunk *next_free;
    struct slab_chunk *prev_free;
    struct slab *slab;
    uintptr_t data_addr;
};

struct slab {
    size_t size;
    size_t num_free;
    struct slab *next;
    struct slab *prev;
    struct slab_chunk base_chunk;
    struct slab_chunk *chunks;
};

struct slab_cache {
    struct slab_cache *next;
    struct slab_cache *prev;
    struct slab slabs_free;
    struct slab slabs_full;
    struct slab slabs_partial;
    size_t size;
    size_t alignment;
};

enum {
    OK = 0,
    ENOMEM = 1,
    EINVAL = 2,
};

typedef struct slab_chunk slab_chunk_t;

#define SLAB_CAPACITY 16

struct slab_cache *caches;

#define to_chunk_ptr(uint_ptr) ((struct slab_chunk *)(void *)(uint_ptr))
#define to_uintptr(ptr) ((uintptr_t)(void *)(ptr))

#define align_up(num, alignment) (((num) - ((num) % (alignment))) + \
    ((((num) % (alignment))) ? (alignment) : 0))

#define page_align_up(num) align_up(num, PAGE_SIZE) 

#define align_down(num, alignment) ((num) - ((num) % (alignment)))

#define page_align_down(num) align_down((num), PAGE_SIZE) 

static inline int slab_remove_from_cache(struct slab *slab) {
    if (!slab) {
        return EINVAL;
    }
    
    if (slab->next) {
        slab->next->prev = slab->prev;
    }
    slab->prev->next = slab->next; 
    return 0;
}

static inline int slab_insert_in_cache(struct slab *slab_list,
    struct slab *slab) {

    if (!slab_list) {
        return EINVAL;
    }

    struct slab *next = slab_list->next;
    slab_list->next = slab; 
    slab->prev = slab_list;
    slab->next = next;

    if (next) {
        next->prev = slab;
    }
    return 0;
}


int slab_alloc_slab_align(struct slab_cache *cache, size_t align) {
    struct slab *new_slab;
    size_t chunk_meta_size;
    size_t slab_data_size;
    void *slab_data;
    
    if (align) {
        size_t slab_chunks_size = (SLAB_CAPACITY + 1) * sizeof(slab_chunk_t);
        chunk_meta_size = cache->size;

        slab_data_size = SLAB_CAPACITY * cache->size;
        new_slab = malloc(sizeof(*new_slab) + slab_chunks_size);
        slab_data = aligned_alloc(align, slab_data_size);
    } else {
        chunk_meta_size = (cache->size + sizeof(struct slab_chunk));

        slab_data_size = ((SLAB_CAPACITY + 1) * chunk_meta_size) - cache->size;
        new_slab = malloc(sizeof(*new_slab) + slab_data_size);
    }

    uintptr_t slab_chunks_addr = to_uintptr(new_slab) + sizeof(*new_slab);
    new_slab->chunks = to_chunk_ptr(slab_chunks_addr);
    struct slab_chunk **cur_chunk = &new_slab->chunks;
    struct slab_chunk *prev_chunk = &new_slab->base_chunk;
    /* printf("Slab alloc\n"); */

    // insert the data chunks linked list inside the malloc
    // allocated space
    for (unsigned int i = 0; i < SLAB_CAPACITY; i++) {
        uintptr_t chunk_addr;
        struct slab_chunk *ch;
        
        if (align) {
            chunk_addr = slab_chunks_addr + (i * sizeof(*ch));
            *cur_chunk = to_chunk_ptr(chunk_addr);

            ch = *cur_chunk;

            ch->data_addr =
                (uintptr_t)slab_data + (i * chunk_meta_size);
            ch->next =
                to_chunk_ptr(chunk_addr + sizeof(slab_chunk_t));
        } else {
            chunk_addr = slab_chunks_addr + (i * chunk_meta_size);

            if (!*cur_chunk) {
                *cur_chunk = to_chunk_ptr(chunk_addr);
            }

            ch = *cur_chunk;
            ch->data_addr = chunk_addr + sizeof(*ch);
            ch->next =
                to_chunk_ptr(chunk_addr + chunk_meta_size);
        }
        
        ch->next_free = ch->next;
        ch->prev_free = prev_chunk;
        ch->slab = new_slab;

        prev_chunk = *cur_chunk;
        cur_chunk = &ch->next;
    }
        
    new_slab->size = chunk_meta_size;
    new_slab->num_free = SLAB_CAPACITY;

    new_slab->base_chunk.data_addr = 0;
    new_slab->base_chunk.slab = new_slab;
    new_slab->base_chunk.next = new_slab->chunks;
    new_slab->base_chunk.next_free = new_slab->chunks;
    new_slab->base_chunk.prev_free = NULL;
    new_slab->next = NULL;
    new_slab->prev = NULL;

    slab_insert_in_cache(&cache->slabs_free, new_slab);

    return 0;
}

struct slab_cache *slab_cache_create_align(size_t size, size_t alignment) {
    struct slab_cache *next_cache = caches;
    caches = calloc(1, sizeof(*caches));
    caches->next = next_cache;
    if (caches->next) {
        caches->next->prev = caches;
    }
    
    if (alignment) {
        caches->size = align_up(size, alignment);
    } else {
        caches->size = size;
    }

    caches->alignment = alignment;
    slab_alloc_slab_align(caches, alignment);
    
    return caches;
}

struct slab_cache *slab_cache_create(size_t size) {
    return slab_cache_create_align(size, 0);
}

void slab_destroy_slab(struct slab_cache *cache, struct slab *slabs) {
    for (; slabs; slabs = slabs->next) {
        free(slabs);

        if (cache->alignment) {
            free((void *)slabs->chunks->data_addr);
        } 
    }
}

void slab_cache_destroy(struct slab_cache *cache) {
    if (cache->prev) {
        cache->prev->next = cache->next;
    }

    if (cache->next) {
        cache->next->prev = cache->prev;
    }

    slab_destroy_slab(cache, cache->slabs_free.next);
    slab_destroy_slab(cache, cache->slabs_partial.next);
    slab_destroy_slab(cache, cache->slabs_full.next);

    free(cache);
}

static void print_all_slabs(struct slab_cache *cache) {

    if (cache->slabs_partial.next) {
        printf("Partial slabs %p\n", cache->slabs_partial.next);
    }
    if (cache->slabs_free.next) {
        printf("Free slabs  %p\n", cache->slabs_free.next);
        printf("Free slabs next %p\n", cache->slabs_free.next->next);
    }
    if (cache->slabs_full.next) {
        printf("Full slabs %p\n", cache->slabs_full.next);
        printf("Full slabs next%p\n", cache->slabs_full.next->next);
    }
}

void *slab_alloc_from_cache(struct slab_cache *cache) {
    if (!cache) {
        return NULL;
    }

    struct slab *non_full_slabs = cache->slabs_partial.next;
    
    if (!non_full_slabs) {
        non_full_slabs = cache->slabs_free.next;
    }

    // no more free or partially free slabs
    if (!non_full_slabs) {
        if (slab_alloc_slab_align(cache, cache->alignment)) {
            return NULL;
        }

        non_full_slabs = cache->slabs_free.next;
    }

    struct slab_chunk **free_chunk = &non_full_slabs->base_chunk.next_free;
    uintptr_t free_addr = ((*free_chunk)->data_addr);

    if ((*free_chunk)->next_free) {
        (*free_chunk)->next_free->prev_free = (*free_chunk)->prev_free;
    }
    *free_chunk = (*free_chunk)->next_free;
    non_full_slabs->num_free--;

    // it was the last free chunk in the slab
    // adding the slab to full slabs linked list
    if (non_full_slabs->num_free == 0) {
        slab_remove_from_cache(cache->slabs_partial.next);
        slab_insert_in_cache(&cache->slabs_full, non_full_slabs);
    } else if (non_full_slabs->num_free == SLAB_CAPACITY - 1) {
        slab_remove_from_cache(cache->slabs_free.next);
        slab_insert_in_cache(&cache->slabs_partial, non_full_slabs);
    }
    
    return (void *)free_addr;
}

bool addr_within_slab(struct slab *slab, uintptr_t addr) {
    uintptr_t slab_data_addr = slab->base_chunk.next->data_addr;
    return (slab_data_addr <= addr
            && (slab_data_addr + (slab->size * SLAB_CAPACITY)) > addr);
}

uintptr_t slab_find_chunk(struct slab *slab, uintptr_t seek_addr) {
    for (; slab; slab = slab->next) {
        if (addr_within_slab(slab, seek_addr)) {
            struct slab_chunk *chunk = slab->chunks->next;

            for(; chunk; chunk = chunk->next) {
                if (chunk->data_addr == seek_addr) {
                    return to_uintptr(chunk);
                }
            }
        }
    }

    return 0;
}

void slab_free(struct slab_cache *cache, void *obj) {
    uintptr_t chunk_addr = 0;
    uintptr_t obj_addr = (uintptr_t)obj;

    if (!cache->alignment) {
        chunk_addr = obj_addr - sizeof(struct slab_chunk);
    } else {
        chunk_addr = slab_find_chunk(cache->slabs_partial.next, obj_addr);

        if (!chunk_addr) {
            chunk_addr = slab_find_chunk(cache->slabs_full.next, obj_addr);
        }

        if (!chunk_addr) {
            return;
        }
    }
    
    struct slab_chunk *chunk = to_chunk_ptr(chunk_addr);
    struct slab_chunk *first_chunk = &chunk->slab->base_chunk;
    chunk->slab->num_free++;

    if (chunk->slab->num_free == 1) {
        /* debug_log("FREE: move to partial list\n"); */
        slab_remove_from_cache(chunk->slab);
        slab_insert_in_cache(&cache->slabs_partial, chunk->slab);
    } else if (chunk->slab->num_free == SLAB_CAPACITY) {
        /* debug_log("FREE: add free slab list\n"); */
        slab_remove_from_cache(chunk->slab);
        slab_insert_in_cache(&cache->slabs_free, chunk->slab);
    }
    
    if (!first_chunk->next_free ||
        to_uintptr(first_chunk->next_free) > to_uintptr(chunk)) {
        chunk->next_free = first_chunk->next_free;
        first_chunk->next_free = chunk;
    } else {
        struct slab_chunk *prev_free = first_chunk->next_free;
        for (; to_uintptr(prev_free->next_free) < to_uintptr(chunk);
             prev_free = prev_free->next_free);

        chunk->next_free = prev_free->next_free;
        prev_free->next_free = chunk;
    }

}

void *slab_alloc(size_t size) {
    struct slab_cache *cache = caches;

    for(; cache && cache->size != size; cache = cache->next); 

    if (!cache) {
        return NULL;
    }
    
    return slab_alloc_from_cache(cache);
}

int slab_alloc_init(uintptr_t base) {
    return 0;
}

void slab_test(void) {
    struct slab_cache *cache = slab_cache_create(0x100);
    void *ps[20];

    for (unsigned int i = 0; i < 20; i++) {
        ps[i] = slab_alloc_from_cache(cache);
        printf("Slab alloc %u: %p\n", i, ps[i]);

        if (!(i % 3)) {
            /* slab_free(cache, ps[i]); */
        }
    }

    slab_free(cache, ps[15]);
    ps[15] = slab_alloc_from_cache(cache);
    printf("Slab alloc: %p\n", ps[15]);

    slab_cache_destroy(cache);
}
