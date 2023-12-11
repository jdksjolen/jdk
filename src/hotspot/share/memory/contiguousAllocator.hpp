#include "memory/allocation.hpp"
#include "runtime/globals.hpp"
#include "nmt/memTracker.hpp"
#include "utilities/align.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/powerOfTwo.hpp"
#include "runtime/os.hpp"

#include <sys/mman.h>
#include <stdlib.h>
#include <new>

#ifndef SHARE_MEMORY_CONTIGUOUSALLOCATOR_HPP
#define SHARE_MEMORY_CONTIGUOUSALLOCATOR_HPP

// Allocates memory into a contiguous fixed-size area at page-sized granularity.
// Explicitly avoids having the OS use huge pages.
class ContiguousAllocator {
public:
  struct AllocationResult { void* loc; size_t sz; };
  static size_t get_chunk_size(bool useHugePages) {
    return align_up(useHugePages ? 2*M : 512*K, os::vm_page_size());
  }
private:

  char* allocate_virtual_address_range(bool useHugePages) {
    constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    char* addr = (char*)::mmap(nullptr, size, PROT_READ|PROT_WRITE, flags, -1, 0);
    if (addr == MAP_FAILED) {
      return nullptr;
    }

    char* aligned_addr = align_up(addr, get_chunk_size(false));
    if (aligned_addr != addr) {
      ::munmap(addr, aligned_addr - addr);
      size -= aligned_addr - addr;
      addr = aligned_addr;
    }

    // Avoid mapping 2MB huge page
    if (is_aligned(addr, 2*M)) {
      const size_t cz = get_chunk_size(false);
      ::munmap(addr, cz);
      addr += cz;
      size -= cz;
    }
    return addr;
  }

  AllocationResult populate_chunk(size_t requested_size) {
    char* next_offset = this->offset + requested_size;
    if (next_offset <= committed_boundary) {
      AllocationResult r{this->offset, requested_size};
      this->offset = next_offset;
      return r;
    }
    size_t chunk_aligned_size = align_up(requested_size, chunk_size);

    if (next_offset >= start + this->size) {
      return {nullptr, 0};
    }

#define MADV_POPULATE_WRITE 23
    int ret = ::madvise(committed_boundary, chunk_aligned_size, MADV_POPULATE_WRITE);
#undef MADV_POPULATE_WRITE
    if (ret == -1) {
      return {nullptr, 0};
    }
    this->committed_boundary += chunk_aligned_size;

    void* addr = this->offset;
    this->offset = next_offset;
    return {addr, requested_size};
  }

public:
  static const size_t default_size = 1*G;
  // The size of unused-but-allocated chunks that we allow before madvising() that they're not needed.
  static const size_t slack = 4;
  MEMFLAGS flag;
  size_t size;
  size_t chunk_size;
  char* start;
  char* offset;
  char* committed_boundary;
  ContiguousAllocator(size_t size, MEMFLAGS flag, bool useHugePages = false)
    : flag(flag), size(size),
      chunk_size(get_chunk_size(false)),
      start(allocate_virtual_address_range(false)),
      offset(align_up(start, chunk_size)),
      committed_boundary(align_up(start, chunk_size)) {
    // Pre-fault first 64k.
    constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE;
    char* addr = (char*)::mmap(this->offset, align_up(512*K, chunk_size), PROT_READ|PROT_WRITE, flags, -1, 0);
    committed_boundary = addr;
  }

  ContiguousAllocator(MEMFLAGS flag, bool useHugePages = false)
    : ContiguousAllocator(default_size, flag, useHugePages) {}

  struct MemoryArea { char* start; size_t size; };
  ContiguousAllocator(MemoryArea ma, MEMFLAGS flag)
    : flag(flag), size(ma.size), chunk_size(get_chunk_size(false)),
      start(ma.start), offset(ma.start), committed_boundary(ma.start) {}

  ~ContiguousAllocator() {
    os::release_memory(start, size);
  }

  AllocationResult alloc(size_t size) {
    return populate_chunk(size);
  }

  // This is a NOP. Use reset_to(void* p) instead.
  void free(void* p) {
  }

  void reset_full(size_t memory_to_leave = 0) {
    if (offset == start) return;
    offset = start;
    // Try to get rid of any huge pages accidentally allocated by doing size - memory
    // instead of committed_boundary
    void* aligned_start = align_up(offset + memory_to_leave, chunk_size);
    const unsigned long aligned_size = align_up(size - memory_to_leave, chunk_size);
    int ret = ::madvise(aligned_start, aligned_size, MADV_DONTNEED);
    assert(ret == 0 || errno == ENOMEM, "must");
    committed_boundary = (char*)aligned_start;
  }

  void reset_to(void* p) {
    assert(p <= offset, "must be");

    void* chunk_aligned_pointer = p;
    offset = (char*)chunk_aligned_pointer;
    size_t unused_bytes = committed_boundary - offset;

    // We don't want to keep around too many pages that aren't in use,
    // so we ask the OS to throw away the physical backing, while keeping the memory reserved.
    if (unused_bytes >= slack*chunk_size) {
      committed_boundary = align_up(offset, chunk_size);
      // Look into MADV_FREE/MADV_COLD
      int ret = ::madvise(committed_boundary, align_up(unused_bytes, chunk_size), MADV_DONTNEED);
      assert(ret == 0, "must");
      // The actual reserved region(s) might not cover this whole area, therefore
      // the reserved region will not be found. We must first register a covering region.
      // Here's another issue: NMT wants the flags to match, but we've got no clue.
      // Just implement a solution for this.
      //MemTracker::record_virtual_memory_reserve(offset, size, CALLER_PC);
      //MemTracker::record_virtual_memory_release(offset, size);
    }
  }
};

#endif // SHARE_MEMORY_CONTIGUOUSALLOCATOR_HPP
