#include "nmt/nativelibs.hpp"

namespace nmt_native {
bool initialized = false;

nmt_string_map* string_map = nullptr;

void nmt_native_initialize() {
  void* p = raw_malloc(sizeof(nmt_string_map));
  new (p) nmt_string_map;
  string_map = (nmt_string_map*)p;
  initialized = true;
}

nmt_string_map* nmt_native_map() {
  return string_map;
}

arena_index make_arena(const char* name) {
  if (!initialized) return -2;
  return nmt_native::string_map->upsert_entry(name);
}
void* arena_alloc(arena_index a, size_t size) {
  if (!initialized) return raw_malloc(size);
  nmt_string_map::header* outer_ptr = (nmt_string_map::header*)nmt_native::raw_malloc(size + sizeof(nmt_string_map::header));
  if (outer_ptr != nullptr) {
    nmt_string_map::entry& e = string_map->entries.at(a);
    e.counter.allocate(size);
    *outer_ptr = nmt_string_map::header{size, a};
  }
  return (void*)(outer_ptr + 1);
}

void arena_free(void* ptr) {
  if (!initialized) return raw_free(ptr);
  nmt_string_map::header* outer_ptr = (nmt_string_map::header*)ptr - 1;
  uint32_t ar = outer_ptr->ar;
  uint32_t sz = outer_ptr->sz;
  string_map->entries.at(ar).counter.deallocate(sz);
  raw_free(outer_ptr);
}

} // namespace nmt_native
