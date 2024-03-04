/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_NMT_NMTNATIVECALLSTACKSTORAGE_HPP
#define SHARE_NMT_NMTNATIVECALLSTACKSTORAGE_HPP

#include "utilities/growableArray.hpp"
#include "utilities/nativeCallStack.hpp"

// Virtual memory regions that are tracked by NMT also has their NativeCallStack tracked.
// The NCS are fairly large, so we store them separately and without duplicates.
// This datastructure consists of an array of pointers to chunks containing a fixed amount of stacks.
// With this set up a stack can be uniquely identified by a pair consisting of the chunk index and the index within the chunk.
// These can be stored in one 4-byte integer by limiting the number of chunks and the static size of a chunk to 2**16.
class NativeCallStackStorage : public CHeapObj<mtNMT> {
private:
  static constexpr int      static_chunk_size = 256;

  struct NCSChunk : public CHeapObj<mtNMT> {
    NativeCallStack stacks[static_chunk_size];
  };
  GrowableArrayCHeap<NCSChunk*, mtNMT> stack_chunks;
  bool is_detailed_mode;

public:
  struct StackIndex {
  private:
    uint16_t _chunk, _index;
  public:
    StackIndex(uint16_t chunk, uint16_t index)
    : _chunk(chunk), _index(index) {
    }
    uint16_t chunk() const {
      return _chunk;
    }
    uint16_t index() const {
      return _index;
    }
    static bool equals(const StackIndex& a, const StackIndex& b) {
      return a.chunk() == b.chunk() && a.index() == b.index();
    }
  };

  StackIndex push(const NativeCallStack& stack) {
    // Not in detailed mode, so not tracking stacks.
    if (!is_detailed_mode) {
      return StackIndex(0,0);
    }
    unsigned int index = stack.calculate_hash() % static_chunk_size;
    for (int i = 0; i < stack_chunks.length(); i++) {
      NCSChunk* chunk = stack_chunks.at(i);
      NativeCallStack& found_stack = chunk->stacks[index];
      if (found_stack.is_empty()) {
        chunk->stacks[index] = stack;
        return StackIndex(i, index);
      }
      if (found_stack.equals(stack)) {
        return StackIndex(i, index);
      }
    }
    int old_len = stack_chunks.length();

    NCSChunk* new_chunk = new NCSChunk();
    new_chunk->stacks[index] = stack;
    stack_chunks.push(new_chunk);
    int chunk = old_len;
    return StackIndex(chunk, index);
  }

  const inline NativeCallStack& get(StackIndex si) {
    return stack_chunks.at(si.chunk())->stacks[si.index()];
  }

  NativeCallStackStorage(bool is_detailed_mode)
  : stack_chunks{1},
    is_detailed_mode(is_detailed_mode) {
    NCSChunk* chunk = new NCSChunk();
    stack_chunks.push(chunk);
  }
};

#endif // SHARE_NMT_NMTNATIVECALLSTACKSTORAGE_HPP
