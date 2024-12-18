#include "nmt/contiguousAllocator.hpp"
#include "nmt/memTracker.hpp"

#include <sys/mman.h>

char* ContiguousAllocator::allocate_virtual_address_range() {
  constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  char* addr = (char*)::mmap(nullptr, _size, PROT_READ | PROT_WRITE, flags, -1, 0);

  if (addr == MAP_FAILED) {
    return nullptr;
  }

  MemTracker::record_virtual_memory_reserve(addr, _size, CURRENT_PC);
  return addr;
}

ContiguousAllocator::AllocationResult ContiguousAllocator::populate_chunk(size_t requested_size) {
  char* next_offset = this->_offset + requested_size;
  if (next_offset <= _committed_boundary) {
    AllocationResult r{this->_offset, requested_size};
    this->_offset = next_offset;
    return r;
  }

  if (next_offset >= _start + this->_size) {
    return {nullptr, 0};
  }

  const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE;
  const size_t chunk_aligned_size = align_up(requested_size, _chunk_size);
  char* addr = (char*)::mmap(this->_offset, chunk_aligned_size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (addr == MAP_FAILED) {
    return {nullptr, 0};
  }
  MemTracker::record_virtual_memory_commit(addr, chunk_aligned_size, CALLER_PC);

  this->_committed_boundary += chunk_aligned_size;

  this->_offset = next_offset;
  return {addr, chunk_aligned_size};
}

ContiguousAllocator::~ContiguousAllocator() {
  if (is_reserved()) {
    unreserve();
  }
  MemTracker::record_virtual_memory_release(_start, _size);
}
