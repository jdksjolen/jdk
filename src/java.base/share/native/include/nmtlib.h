#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>

typedef struct allocator_t allocator;
allocator *make_allocator(const char* name);
void free_allocator(allocator *alloc);
void* alloc(allocator* a, size_t sz);
void alloc_free(allocator* a, void* inner_ptr);
