#include "nmt/contiguousAllocator.hpp"

char* ContiguousAllocator::allocate_virtual_address_range() {
  if (_size == 0) return nullptr;
  constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  char* addr = (char*)::mmap(nullptr, _size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (addr == MAP_FAILED) {
    return nullptr;
  }

  char* aligned_addr = align_up(addr, get_chunk_size());
  if (aligned_addr != addr) {
    ::munmap(addr, aligned_addr - addr);
    _size -= aligned_addr - addr;
    addr = aligned_addr;
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

  constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE;
  const size_t chunk_aligned_size = align_up(requested_size, get_chunk_size());
  char* addr =
      (char*)::mmap(this->_offset, chunk_aligned_size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (addr == MAP_FAILED) {
    return {nullptr, 0};
  }

  this->_committed_boundary += chunk_aligned_size;

  this->_offset = next_offset;
  return {addr, chunk_aligned_size};
}
