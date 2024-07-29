#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>

typedef struct allocator_t {
  const char* name;
  uint64_t allocated;
  uint64_t peak;
} allocator;

struct header {
  size_t sz;
};

allocator *make_allocator(const char* name) {
  allocator *alloc = (allocator*)malloc(sizeof(allocator));
  alloc->name = name;
  alloc->allocated = 0;
  alloc->peak = 0;
  return alloc;
}

void free_allocator(allocator *alloc) {
  free(alloc);
}

void* alloc(allocator* a, size_t sz) {
  char* ptr = (char*)malloc(sz + sizeof(struct header));
  if (ptr == NULL) return NULL;

  a->allocated += sz;
  if (a->allocated > a->peak) {
    a->peak = a->allocated;
  }
  ((struct header*)ptr)->sz = sz;
  return ptr+sizeof(struct header);
}

void alloc_free(allocator* a, void* inner_ptr) {
  if (inner_ptr == NULL) return;

  char* outer_ptr = (char*)inner_ptr-sizeof(struct header);
  struct header* header = (struct header*)outer_ptr;
  a->allocated -= header->sz;
  free(outer_ptr);
  return;
}
