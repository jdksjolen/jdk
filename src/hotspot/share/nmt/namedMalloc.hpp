// Named allocator API
#include "nmt/memTag.hpp"
#include "nmt/memTagNameTable.hpp"
#include "nmt/mallocTracker.hpp"
#include "runtime/oshpp"

struct NamedMalloc {
  MemTag tag;

  NamedMalloc(const char* name) {
    // TODO: Needs global NMT mutex
    bool found = false;
    MemTag tag = (MemTag)MemTagNameTable::Instance::get(name, &found);
    if (found) {
      this->tag = tag;
    } else {
      MemTag new_tag = MallocMemorySummary::new_tag();
      MemTagNameTable::Instance::put(new_tag, name);
      this->tag = new_tag;
    }
  }
  NamedMalloc(const NamedMalloc& other) : tag(other.tag) {}

  void operator=(const NamedMalloc& other) = delete;

  void* malloc(size_t sz) {
    return os::malloc(sz, this->tag);
  }
  void free(void* ptr) {
    return os::free(ptr);
  }
};
