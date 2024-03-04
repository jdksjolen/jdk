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

#ifndef SHARE_NMT_VIRTUALMEMORYVIEW_HPP
#define SHARE_NMT_VIRTUALMEMORYVIEW_HPP

#include "memory/allocation.hpp"
#include "nmt/nmtNativeCallStackStorage.hpp"
#include "nmt/nmtCommon.hpp"
#include "nmt/vmatree.hpp"
#include "nmt/virtualMemoryTracker.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/nativeCallStack.hpp"
#include "utilities/ostream.hpp"

/*
  Remaining issues:
  3. No baseline summary diffing
  4. No baseline detail diffing
  4. Reporting not part of Reporter class but part of VirtualMemoryView, not too bad.
  5. Insufficient amount of unit tests
  6. Need to fix includes, copyright stmts etc
*/

class VirtualMemoryView {
  friend class VirtualMemoryViewTest;

  using Id = int32_t;
  static constexpr const Id no_id = -1;
public:

  struct PhysicalMemorySpace {
    Id id; // Uniquely identifies the device
    static Id unique_id;
    static Id next_unique() {
      return unique_id++;
    }
  };

  struct PhysicalMemoryData {
    NativeCallStackStorage::StackIndex stack_idx;
    MEMFLAGS flag;
    PhysicalMemoryData() : stack_idx(0,0), flag(mtNone) {
    }
    PhysicalMemoryData(NativeCallStackStorage::StackIndex stack_idx, MEMFLAGS flag) : stack_idx(stack_idx), flag(flag) {
    }
    static bool equals(const PhysicalMemoryData& a, const PhysicalMemoryData& b) {
      return NativeCallStackStorage::StackIndex::equals(a.stack_idx, b.stack_idx) && a.flag == b.flag;
    }
  };

  struct Mapping {
    Id id;
    bool is_mapped() { return id > 0; }
    Mapping() : id{no_id} {}
    Mapping(Id id) : id{id} {};
  };

  struct VirtualMemoryData {
    NativeCallStackStorage::StackIndex stack_idx;
    MEMFLAGS flag;
    Mapping mapping;  // Only present if RangeType == Mapped
    VirtualMemoryData()
    : stack_idx(0, 0),
      flag(mtNone),
      mapping(no_id) {}
    VirtualMemoryData(NativeCallStackStorage::StackIndex stack_idx, MEMFLAGS flag = mtNone,
                     Id id = no_id)
      : stack_idx(stack_idx),
        flag(flag),
        mapping{id} {
    }
    static bool equals(const VirtualMemoryData& a, const VirtualMemoryData& b) {
      return (NativeCallStackStorage::StackIndex::equals(a.stack_idx,b.stack_idx) &&
              (a.flag == b.flag) && (a.mapping.id == b.mapping.id));
    };
  };

  using VirtualRegionStorage = VMATree<VirtualMemoryData, VirtualMemoryData::equals>;
  using PhysicalRegionStorage = GrowableArrayCHeap<VMATree<PhysicalMemoryData, PhysicalMemoryData::equals>,
                                                    mtNMT>;

  struct TrackedProcessMemory : public CHeapObj<mtNMT> {
    // Reserved memory within this process' memory map.
    VirtualRegionStorage virtual_regions;
    // Committed memory per PhysicalMemorySpace
    PhysicalRegionStorage physical_devices;
    // Summary tracking per PhysicalMemorySpace
    GrowableArrayCHeap<VirtualMemorySnapshot, mtNMT> summary;
    TrackedProcessMemory();
    TrackedProcessMemory(const TrackedProcessMemory& other);
    // Deep copying of VirtualMemory
    TrackedProcessMemory& operator=(const TrackedProcessMemory& other);
  };

private:
  // Data and API
  TrackedProcessMemory _virt_mem;
  NativeCallStackStorage _stack_storage;
public:
  void initialize(bool is_detailed_mode);

  void reserve_memory(address base_addr, size_t size, MEMFLAGS flag, const NativeCallStack& stack);
  void commit_memory(address base_addr, size_t size, const NativeCallStack& stack);
  void release_memory(address base_addr, size_t size);

  void allocate_memory_into_space(const PhysicalMemorySpace space, address offset, size_t size,
                                MEMFLAGS flag,
                                const NativeCallStack& stack);
  void free_memory_into_space(const PhysicalMemorySpace& space, address offset, size_t size);

  void add_mapping_into_space(const PhysicalMemorySpace& space, address base_addr, size_t size,
                           address offset, MEMFLAGS flag, const NativeCallStack& stack);

  void remove_mapping_into_space(const PhysicalMemorySpace& space, address base_addr, size_t size);

  // Produce a report on output.
  void report(TrackedProcessMemory& mem, outputStream* output, size_t scale = K);
  const TrackedProcessMemory& virtual_memory() {
    return _virt_mem;
  }

  // Compute the summary snapshot of a VirtualMemory state.
  void compute_summary_snapshot(TrackedProcessMemory& vmem);

public:
  VirtualMemoryView(bool is_detailed_mode);

  class Interface : public AllStatic {
    // A default PhysicalMemorySpace for when allocating to the heap.
    static PhysicalMemorySpace _heap;
    static VirtualMemoryView* _instance;
    static GrowableArrayCHeap<const char*, mtNMT>* _names; // Map memory space to name
  public:
    static void initialize(bool is_detailed_mode);

    static PhysicalMemorySpace register_space(const char* descriptive_name);

    static void reserve_memory(address base_addr, size_t size, MEMFLAGS flag,
                               const NativeCallStack& stack);
    static void release_memory(address base_addr, size_t size);
    static void commit_memory(address base_addr, size_t size, const NativeCallStack& stack);
    static void uncommit_memory(address base_addr, size_t size);

    static void add_view_into_space(const PhysicalMemorySpace& space, address base_addr,
                                    size_t size, address offset, MEMFLAGS flag,
                                    const NativeCallStack& stack);
    static void remove_view_into_space(const PhysicalMemorySpace& space, address base_addr,
                                       size_t size);

    static void allocate_memory_into_space(const PhysicalMemorySpace& space, address offset,
                                         size_t size, const NativeCallStack& stack);
    static void uncommit_memory_into_space(const PhysicalMemorySpace& space, address offset,
                                           size_t size);

    // Produce a report on output.
    static void report(TrackedProcessMemory& mem, outputStream* output, size_t scale = K);
    static const TrackedProcessMemory& virtual_memory() {
      return _instance->_virt_mem;
    }

    // Compute the summary snapshot of a VirtualMemory state.
    static void compute_summary_snapshot(TrackedProcessMemory& vmem);
  };
};


#endif // SHARE_NMT_VIRTUALMEMORYVIEW_HPP
