/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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

#include "memory/allocation.hpp"
#include "nmt/memTracker.hpp"
#include "nmt/virtualMemoryTracker.hpp"
#include "runtime/globals.hpp"
#include "nmt/memTag.hpp"
#include "utilities/align.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/powerOfTwo.hpp"
#include "runtime/os.hpp"

#include <sys/mman.h>
#include <stdlib.h>

#ifndef SHARE_NMT_CONTIGUOUSALLOCATOR_HPP
#define SHARE_NMT_CONTIGUOUSALLOCATOR_HPP

// TODO: Doesn't do any NMT accounting.
class ContiguousAllocator {
public:
  struct AllocationResult {
    void* loc;
    size_t sz;
    bool failed() {
      return loc == nullptr &&  sz == 0;
    }
  };

  static size_t get_chunk_size() {
    return os::vm_page_size();
  }

private:
  char* allocate_virtual_address_range();
  AllocationResult populate_chunk(size_t requested_size);

public:
  MemTag _flag;
  size_t _size;
  size_t _chunk_size;
  char* _start; // Start of memory
  char* _offset; // Last returned point of allocation
  char* _committed_boundary; // Anything above this must be ::mmapped in with populate
  ContiguousAllocator(size_t size, MemTag flag)
  : _flag(flag), _size(align_up(size, os::vm_page_size())),
    _chunk_size(get_chunk_size()),
    _start(allocate_virtual_address_range()),
    _offset(align_up(_start, _chunk_size)),
    _committed_boundary(align_up(_start, _chunk_size)) {
    if (size == 0) return;

    constexpr const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE;
    char* addr = (char*)::mmap(this->_offset, _chunk_size, PROT_READ|PROT_WRITE, flags, -1, 0);
    _committed_boundary = addr;
  }

  ContiguousAllocator(const ContiguousAllocator& other)
  : _flag(other._flag),
    _size(other._size),
    _chunk_size(get_chunk_size()),
    _start(allocate_virtual_address_range()),
    _offset(align_up(_start, _chunk_size)),
    _committed_boundary(align_up(_start, _chunk_size)) {
    this->alloc(_size);
    memcpy(this->_start, other._start, other._size);
  }

  ~ContiguousAllocator() {
    ::munmap(_start, _size);
    MemTracker::record_virtual_memory_release(_start, _size);
  }

  AllocationResult alloc(size_t size) {
    return populate_chunk(size);
  }

  // This is a no-op.
  void free(void* p) {
  }

  size_t size() const { return _size; }

  char* at_offset(size_t offset) {
    return _start + offset;
  }
};

#undef SP_CALLER_PC

#endif // SHARE_NMT_CONTIGUOUSALLOCATOR_HPP
