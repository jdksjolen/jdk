// NMT for native libraries

#ifndef SHARE_NMT_NATIVELIBS_HPP
#define SHARE_NMT_NATIVELIBS_HPP

#include "nmt/mallocTracker.hpp"
#include "utilities/compilerWarnings.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nmt_native {
static void* raw_malloc(size_t s) {
  ALLOW_C_FUNCTION(::malloc, return ::malloc(s);)
}
static void* raw_realloc(void* old, size_t s) {
  ALLOW_C_FUNCTION(::realloc, return ::realloc(old, s);)
}
static void raw_free(void* p) {
  ALLOW_C_FUNCTION(::free, ::free(p);)
}

template<typename T>
class resizable_array {
  using I = int32_t;
  using BackingElement = T;
  bool _fixed_size;
  I _len;
  I _cap;
  T* _data;
  static const I nil = -1;

  bool grow() {
    if (_cap == std::numeric_limits<I>::max() - 1) {
      // Already at max capacity.
      return false;
    }

    // Widen the capacity temporarily.
    uint64_t widened_cap = static_cast<uint64_t>(_cap);
    if (std::numeric_limits<uint64_t>::max() - widened_cap < widened_cap) {
      // Overflow of uint64_t in case of resize, we fail.
      return false;
    }
    // Safe to double the widened_cap
    widened_cap *= 2;
    // If I has max size (2**X) - 1, is cap at 2**(X-1)?
    if (std::numeric_limits<I>::max() - _cap == (_cap - 1)) {
      // Reduce widened_cap
      widened_cap -= 1;
    }

    I next_cap = static_cast<I>(widened_cap);
    void* next_array = raw_realloc(_data, next_cap * sizeof(BackingElement));
    if (next_array == nullptr) {
      return false;
    }
    _data = static_cast<BackingElement*>(next_array);
    _cap = next_cap;
    return true;
  }

public:
  resizable_array(I initial_cap)
    : _fixed_size(false),
      _len(0),
      _cap(initial_cap),
      _data(static_cast<BackingElement*>(raw_malloc(initial_cap * sizeof(BackingElement)))) {
  }

  resizable_array(BackingElement* data, I capacity)
    : _fixed_size(true),
      _len(0),
      _cap(capacity),
      _data(data) {
  }

  ~resizable_array() {
    if (!_fixed_size) {
      raw_free(_data);
    }
  }

  I length() {
    return _len;
  }

  BackingElement& at(I i) {
    assert(i < _len, "oob");
    return _data[i];
  }

  BackingElement* adr_at(I i) {
    return &at(i);
  }

  I append() {
    if (_len == _cap) {
      if (_fixed_size) return nil;
      if (!grow()) {
        return nil;
      }
    }
    I idx = _len++;
    return idx;
  }

  void remove_last() {
    I idx = _len - 1;
    --_len;
  }
};

struct nmt_string_map {
  void* operator new(std::size_t, void* p) {
    return p;
  }
  struct entry {
    void* operator new(std::size_t, void* p) {
      return p;
    }

    const char* name;
    MemoryCounter counter;
    entry()
      : name(nullptr),
        counter() {
    }
    entry(const char* name)
      : name(name),
        counter() {
    }
  };

  struct header {
    size_t sz;
    int32_t ar;
  };

  resizable_array<entry> entries;

  nmt_string_map()
    : entries(8) {
  }

  int32_t upsert_entry(const char* name) {
    for (int i = 0; i < entries.length(); i++) {
      if (::strcmp(name, entries.at(i).name) == 0) {
        return i;
      }
    }
    auto i = entries.append();
    new (entries.adr_at(i)) entry(name);
    return i;
  }
};
void nmt_native_initialize();
using arena_index = int32_t;

nmt_string_map* nmt_native_map();

arena_index make_arena(const char* name);

void* arena_alloc(arena_index a, size_t size);
void arena_free(void* ptr);
} // namespace nmt_native

#endif // SHARE_NMT_NATIVELIBS_HPP
