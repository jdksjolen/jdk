/*
 * Copyright (c) 2017, 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_MEMORY_ARENA_HPP
#define SHARE_MEMORY_ARENA_HPP

#include "memory/allocation.hpp"
#include "memory/contiguousAllocator.hpp"
#include "runtime/globals.hpp"
#include "logging/log.hpp"
#include "runtime/threadCritical.hpp"
#include "nmt/memTracker.hpp"
#include "utilities/align.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/powerOfTwo.hpp"
#include "runtime/os.hpp"
#include <new>

class ArenaMemoryProvider;
class ContiguousProvider;

// The byte alignment to be used by Arena::Amalloc.
#define ARENA_AMALLOC_ALIGNMENT BytesPerLong
#define ARENA_ALIGN(x) (align_up((x), ARENA_AMALLOC_ALIGNMENT))

  // Linked list of raw memory chunks
class Chunk {

 private:
  Chunk*       _next;     // Next Chunk in list
  const size_t _len;      // Size of this Chunk
public:
  NONCOPYABLE(Chunk);

  void operator delete(void*) = delete;
  void* operator new(size_t) = delete;

  Chunk(size_t length);

  enum {
    // default sizes; make them slightly smaller than 2**k to guard against
    // buddy-system style malloc implementations
    // Note: please keep these constants 64-bit aligned.
#ifdef _LP64
    slack      = 40,            // [RGV] Not sure if this is right, but make it
                                //       a multiple of 8.
#else
    slack      = 24,            // suspected sizeof(Chunk) + internal malloc headers
#endif

    tiny_size  =  4*K - 16, // Size of first chunk (tiny)
    init_size  =  8*K - 16, // Size of first chunk (normal aka small)
    medium_size= 16*K - 16, // Size of medium-sized chunk
    size       = 32*K - 16, // Default size of an Arena chunk (following the first)
    non_pool_size = init_size + 4*K // An initial size which is not one of above
  };

  static void chop(Chunk* chunk, ArenaMemoryProvider* mp);      // Chop this chunk
  static void next_chop(Chunk* chunk, ArenaMemoryProvider* mp); // Chop next chunk
  static size_t aligned_overhead_size(void) { return ARENA_ALIGN(sizeof(Chunk)); }
  static size_t aligned_overhead_size(size_t byte_size) { return ARENA_ALIGN(byte_size); }

  size_t length() const         { return _len;  }
  Chunk* next() const           { return _next;  }
  void set_next(Chunk* n)       { _next = n;  }
  // Boundaries of data area (possibly unused)
  char* bottom() const          { return ((char*) this) + aligned_overhead_size();  }
  char* top()    const          { return bottom() + _len; }
  bool contains(char* p) const  { return bottom() <= p && p <= top(); }
};

class ArenaMemoryProvider : public StackObj {
public:
  struct AllocationResult {
    void* loc;
    size_t sz;
  };

  virtual AllocationResult alloc(AllocFailType alloc_failmode, size_t bytes, size_t length, MEMFLAGS flags) = 0;
  virtual void free(void* ptr) = 0;
  // Is this provider capable of freeing its memory on destruction?
  virtual bool self_free() = 0;
  virtual bool reset_to(void* ptr) = 0;
  virtual bool reset_full(size_t memory_to_leave) = 0;

  Chunk* allocate_chunk(size_t length, AllocFailType alloc_failmode) {
    size_t bytes = ARENA_ALIGN(sizeof(Chunk)) + length;
    assert(is_aligned(length, ARENA_AMALLOC_ALIGNMENT),
           "chunk payload length misaligned: " SIZE_FORMAT ".", length);
    ArenaMemoryProvider::AllocationResult res = alloc(alloc_failmode, bytes, length, mtChunk);
    if (res.loc == nullptr) {
      return nullptr;
    }
    return ::new (res.loc) Chunk(res.sz - sizeof(Chunk));
  }

  void deallocate_chunk(Chunk* p) {
    this->free(p);
  }
};

class ContiguousProvider final : public ArenaMemoryProvider {
  ContiguousAllocator _cont_allocator;
public:
  explicit ContiguousProvider(MEMFLAGS flag, bool useHugePages) :
    _cont_allocator(flag, useHugePages) {}
  explicit ContiguousProvider(MEMFLAGS flag) :
    _cont_allocator(flag) {}
  explicit ContiguousProvider(MEMFLAGS flag, size_t max_size) :
    _cont_allocator(max_size, flag) {}
  explicit ContiguousProvider(ContiguousAllocator::MemoryArea ma, MEMFLAGS flag) :
    _cont_allocator(ma, flag) {}

  AllocationResult alloc(AllocFailType alloc_failmode, size_t bytes, size_t length, MEMFLAGS flags) override {
    ContiguousAllocator::AllocationResult p = _cont_allocator.alloc(bytes);
     if (p.loc != nullptr) {
       return {p.loc, p.sz};
     }
     if (alloc_failmode == AllocFailStrategy::EXIT_OOM) {
       vm_exit_out_of_memory(bytes, OOM_MALLOC_ERROR, "ContiguousAllocator::alloc");
     }
     return AllocationResult{nullptr, 0};
  }
  void free(void* ptr) override {
    // NOP.
  }

  bool reset_to(void* ptr) override {
    _cont_allocator.reset_to(ptr);
    return true;
 }
  bool reset_full(size_t memory_to_leave = 0) override {
    _cont_allocator.reset_full(memory_to_leave);
    return true;
  }
  bool self_free() override { return true; }
  size_t used() {
    return _cont_allocator.offset - _cont_allocator.start;
  }
};

class ChunkPoolProvider final : public ArenaMemoryProvider {
public:
  AllocationResult alloc(AllocFailType alloc_failmode, size_t bytes, size_t length, MEMFLAGS flags) override;
  void free(void* p) override;
  bool self_free() override;
  bool reset_full(size_t memory_to_leave) override;
  bool reset_to(void* ptr) override;
};

// Fast allocation of memory
class Arena : public CHeapObjBase {
protected:
  static ChunkPoolProvider chunk_pool;
public:

  enum class Tag {
    tag_other = 0,
    tag_ra,   // resource area
    tag_ha,   // handle area
    tag_node  // C2 Node arena
  };

protected:
  friend class HandleMark;
  friend class NoHandleMark;
  friend class VMStructs;

  ArenaMemoryProvider* _mem;
  MEMFLAGS    _flags;           // Memory tracking flags
  const Tag _tag;
  Chunk* _first;                // First chunk
  Chunk* _chunk;                // current chunk
  char* _hwm;                   // High water mark
  char* _max;                   // and max in current chunk
  // Get a new Chunk of at least size x
  void* grow(size_t x, AllocFailType alloc_failmode = AllocFailStrategy::EXIT_OOM);
  size_t _size_in_bytes;        // Size of arena (used for native memory tracking)

  void* internal_amalloc(size_t x, AllocFailType alloc_failmode = AllocFailStrategy::EXIT_OOM)  {
    assert(is_aligned(x, BytesPerWord), "misaligned size");
    if (pointer_delta(_max, _hwm, 1) >= x) {
      char *old = _hwm;
      _hwm += x;
      return old;
    } else {
      return grow(x, alloc_failmode);
    }
  }

 public:
  Arena(MEMFLAGS memflag,  Tag tag = Tag::tag_other);
  Arena(MEMFLAGS memflag, Tag tag, size_t init_size);
  Arena(MEMFLAGS memflag,ContiguousProvider* mp, Tag tag = Tag::tag_other);

  enum class ArenaProvider {
    ChunkPool,
    ContiguousAllocator
  };
  Arena(MEMFLAGS memflag, ArenaProvider provider, Tag tag = Tag::tag_other);
  void init_memory_provider(ArenaMemoryProvider* mem, size_t init_size = Chunk::init_size);
  // Start the chunk_pool cleaner task
  static void start_chunk_pool_cleaner_task();
  ~Arena();
  void  destruct_contents();
  char* hwm() const             { return _hwm; }

  // Fast allocate in the arena.  Common case aligns to the size of jlong which is 64 bits
  // on both 32 and 64 bit platforms. Required for atomic jlong operations on 32 bits.
  void* Amalloc(size_t x, AllocFailType alloc_failmode = AllocFailStrategy::EXIT_OOM) {
    x = ARENA_ALIGN(x);  // note for 32 bits this should align _hwm as well.
    // Amalloc guarantees 64-bit alignment and we need to ensure that in case the preceding
    // allocation was AmallocWords. Only needed on 32-bit - on 64-bit Amalloc and AmallocWords are
    // identical.
    assert(is_aligned(_max, ARENA_AMALLOC_ALIGNMENT), "chunk end unaligned?");
    NOT_LP64(_hwm = ARENA_ALIGN(_hwm));
    return internal_amalloc(x, alloc_failmode);
  }

  // Allocate in the arena, assuming the size has been aligned to size of pointer, which
  // is 4 bytes on 32 bits, hence the name.
  void* AmallocWords(size_t x, AllocFailType alloc_failmode = AllocFailStrategy::EXIT_OOM) {
    assert(is_aligned(x, BytesPerWord), "misaligned size");
    return internal_amalloc(x, alloc_failmode);
  }

  // Fast delete in area.  Common case is: NOP (except for storage reclaimed)
  bool Afree(void *ptr, size_t size) {
    if (ptr == nullptr) {
      return true; // as with free(3), freeing null is a noop.
    }
#ifdef ASSERT
    if (ZapResourceArea) memset(ptr, badResourceValue, size); // zap freed memory
#endif
    if (((char*)ptr) + size == _hwm) {
      _hwm = (char*)ptr;
      if (_hwm == _chunk->bottom()) {
        Chunk* n_current = _first;
        int n = 0;
        while (n_current->next() != _chunk) {
          n_current = n_current->next();
          n++;
        }
        log_info(mmu)("FREE_CHUNK! %d with size %zu", n, _chunk->length());
        Chunk::chop(_chunk, _mem);
        _chunk = n_current;
      }
      return true;
    } else {
      // Unable to fast free, so we just drop it.
      return false;
    }
  }

  void *Arealloc( void *old_ptr, size_t old_size, size_t new_size,
      AllocFailType alloc_failmode = AllocFailStrategy::EXIT_OOM);

  // Determine if pointer belongs to this Arena or not.
  bool contains( const void *ptr ) const;

  // Total of all chunks in use (not thread-safe)
  size_t used() const;

  // Total # of bytes used
  size_t size_in_bytes() const         {  return _size_in_bytes; };
  void set_size_in_bytes(size_t size);

  Tag get_tag() const { return _tag; }

private:
  // Reset this Arena to empty, access will trigger grow if necessary
  void reset(void) {
    _first = _chunk = nullptr;
    _hwm = _max = nullptr;
    set_size_in_bytes(0);
  }
};

// One of the following macros must be used when allocating
// an array or object from an arena
#define NEW_ARENA_ARRAY(arena, type, size) \
  (type*) (arena)->Amalloc((size) * sizeof(type))

#define REALLOC_ARENA_ARRAY(arena, type, old, old_size, new_size)    \
  (type*) (arena)->Arealloc((char*)(old), (old_size) * sizeof(type), \
                            (new_size) * sizeof(type) )

#define FREE_ARENA_ARRAY(arena, type, old, size) \
  (arena)->Afree((char*)(old), (size) * sizeof(type))

#define NEW_ARENA_OBJ(arena, type) \
  NEW_ARENA_ARRAY(arena, type, 1)

#endif // SHARE_MEMORY_ARENA_HPP
