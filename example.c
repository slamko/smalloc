#include "smalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct alloc_struct {
    int a;
    int b;
    void *buf;
    char c;
    struct alloc_struct *next;
};

#define ALLOC_COUNT 120424

void slab_benchmark() {
    struct slab_cache *slab_cache = slab_cache_create(sizeof(struct alloc_struct));

    long int start_time = clock();
    
    struct alloc_struct *ptrs[ALLOC_COUNT]; 
    for (unsigned int i = 0; i < ALLOC_COUNT; i++) {
        ptrs[i] = slab_alloc_from_cache(slab_cache);
        *ptrs[i] = (struct alloc_struct){
            .a = 45,
            .b = 998,
            .buf = NULL,
            .c = 'a',
            .next = ptrs[i],
        };
    }

    for (unsigned int i = 0; i < ALLOC_COUNT; i++) {
        slab_free(slab_cache, ptrs[i]);
    }

    long int end_time = clock();
    long int time_elapsed = end_time - start_time;
    
    printf("Time elapsed: %ld\n", time_elapsed);

    slab_cache_destroy(slab_cache);
}

void malloc_benchmark() {
    long int start_time = clock();

    struct alloc_struct *ptrs[ALLOC_COUNT]; 
    for (unsigned int i = 0; i < ALLOC_COUNT; i++) {
        ptrs[i] = malloc(sizeof **ptrs);
        *ptrs[i] = (struct alloc_struct){
            .a = 45,
            .b = 998,
            .buf = NULL,
            .c = 'a',
            .next = ptrs[i],
        };
    }

    for (unsigned int i = 0; i < ALLOC_COUNT; i++) {
        free(ptrs[i]);
    }

    long int end_time = clock();
    long int time_elapsed = end_time - start_time;

    printf("Time elapsed: %ld\n", time_elapsed);
}

int main() {
    /* malloc_benchmark(); */
    slab_benchmark();
   
    return 0;
}
