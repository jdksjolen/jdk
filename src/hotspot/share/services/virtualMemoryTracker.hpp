/*
 * Copyright (c) 2013, 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_SERVICES_VIRTUALMEMORYTRACKER_HPP
#define SHARE_SERVICES_VIRTUALMEMORYTRACKER_HPP

#include "memory/allocation.hpp"
#include "memory/metaspace.hpp" // For MetadataType
#include "memory/metaspaceStats.hpp"
#include "services/allocationSite.hpp"
#include "services/nmtCommon.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/linkedlist.hpp"
#include "utilities/nativeCallStack.hpp"
#include "utilities/ostream.hpp"


/*
 * Virtual memory counter
 */
class VirtualMemory {
 private:
  size_t     _reserved;
  size_t     _committed;

 public:
  VirtualMemory() : _reserved(0), _committed(0) { }

  inline void reserve_memory(size_t sz) { _reserved += sz; }
  inline void commit_memory (size_t sz) {
    _committed += sz;
    assert(_committed <= _reserved, "Sanity check");
  }

  inline void release_memory (size_t sz) {
    assert(_reserved >= sz, "Negative amount");
    _reserved -= sz;
  }

  inline void uncommit_memory(size_t sz) {
    assert(_committed >= sz, "Negative amount");
    _committed -= sz;
  }

  inline size_t reserved()  const { return _reserved;  }
  inline size_t committed() const { return _committed; }
};

// Virtual memory allocation site, keeps track where the virtual memory is reserved.
class VirtualMemoryAllocationSite : public AllocationSite {
  VirtualMemory _c;
 public:
  VirtualMemoryAllocationSite(const NativeCallStack& stack, MEMFLAGS flag) :
    AllocationSite(stack, flag) { }

  inline void reserve_memory(size_t sz)  { _c.reserve_memory(sz);  }
  inline void commit_memory (size_t sz)  { _c.commit_memory(sz);   }
  inline void uncommit_memory(size_t sz) { _c.uncommit_memory(sz); }
  inline void release_memory(size_t sz)  { _c.release_memory(sz);  }
  inline size_t reserved() const  { return _c.reserved(); }
  inline size_t committed() const { return _c.committed(); }
};

class VirtualMemorySummary;

// This class represents a snapshot of virtual memory at a given time.
// The latest snapshot is saved in a static area.
class VirtualMemorySnapshot : public ResourceObj {
  friend class VirtualMemorySummary;

 private:
  VirtualMemory  _virtual_memory[mt_number_of_types];

 public:
  inline VirtualMemory* by_type(MEMFLAGS flag) {
    int index = NMTUtil::flag_to_index(flag);
    return &_virtual_memory[index];
  }

  inline const VirtualMemory* by_type(MEMFLAGS flag) const {
    int index = NMTUtil::flag_to_index(flag);
    return &_virtual_memory[index];
  }

  inline size_t total_reserved() const {
    size_t amount = 0;
    for (int index = 0; index < mt_number_of_types; index ++) {
      amount += _virtual_memory[index].reserved();
    }
    return amount;
  }

  inline size_t total_committed() const {
    size_t amount = 0;
    for (int index = 0; index < mt_number_of_types; index ++) {
      amount += _virtual_memory[index].committed();
    }
    return amount;
  }

  void copy_to(VirtualMemorySnapshot* s) {
    for (int index = 0; index < mt_number_of_types; index ++) {
      s->_virtual_memory[index] = _virtual_memory[index];
    }
  }
};

class VirtualMemorySummary : AllStatic {
 public:
  static void initialize();

  static inline void record_reserved_memory(size_t size, MEMFLAGS flag) {
    as_snapshot()->by_type(flag)->reserve_memory(size);
  }

  static inline void record_committed_memory(size_t size, MEMFLAGS flag) {
    as_snapshot()->by_type(flag)->commit_memory(size);
  }

  static inline void record_uncommitted_memory(size_t size, MEMFLAGS flag) {
    as_snapshot()->by_type(flag)->uncommit_memory(size);
  }

  static inline void record_released_memory(size_t size, MEMFLAGS flag) {
    as_snapshot()->by_type(flag)->release_memory(size);
  }

  // Move virtual memory from one memory type to another.
  // Virtual memory can be reserved before it is associated with a memory type, and tagged
  // as 'unknown'. Once the memory is tagged, the virtual memory will be moved from 'unknown'
  // type to specified memory type.
  static inline void move_reserved_memory(MEMFLAGS from, MEMFLAGS to, size_t size) {
    as_snapshot()->by_type(from)->release_memory(size);
    as_snapshot()->by_type(to)->reserve_memory(size);
  }

  static inline void move_committed_memory(MEMFLAGS from, MEMFLAGS to, size_t size) {
    as_snapshot()->by_type(from)->uncommit_memory(size);
    as_snapshot()->by_type(to)->commit_memory(size);
  }

  static void snapshot(VirtualMemorySnapshot* s);

  static VirtualMemorySnapshot* as_snapshot() {
    return (VirtualMemorySnapshot*)_snapshot;
  }

 private:
  static size_t _snapshot[CALC_OBJ_SIZE_IN_TYPE(VirtualMemorySnapshot, size_t)];
};



/*
 * A virtual memory region
 */
class VirtualMemoryRegion {
 private:
  address      _base_address;
  size_t       _size;

 public:
  VirtualMemoryRegion(address addr, size_t size) :
    _base_address(addr), _size(size) {
     assert(addr != nullptr, "Invalid address");
     assert(size > 0, "Invalid size");
   }

  inline address base() const { return _base_address;   }
  inline address end()  const { return base() + size(); }
  inline size_t  size() const { return _size;           }

  inline bool is_empty() const { return size() == 0; }

  inline bool contain_address(address addr) const {
    return (addr >= base() && addr < end());
  }


  inline bool contain_region(address addr, size_t size) const {
    return contain_address(addr) && contain_address(addr + size - 1);
  }

  inline bool same_region(address addr, size_t sz) const {
    return (addr == base() && sz == size());
  }


  inline bool overlap_region(address addr, size_t sz) const {
    assert(sz > 0, "Invalid size");
    assert(size() > 0, "Invalid size");
    return MAX2(addr, base()) < MIN2(addr + sz, end());
  }

  inline bool adjacent_to(address addr, size_t sz) const {
    return (addr == end() || (addr + sz) == base());
  }

  void exclude_region(address addr, size_t sz) {
    assert(contain_region(addr, sz), "Not containment");
    assert(addr == base() || addr + sz == end(), "Can not exclude from middle");
    size_t new_size = size() - sz;

    if (addr == base()) {
      set_base(addr + sz);
    }
    set_size(new_size);
  }

  void expand_region(address addr, size_t sz) {
    assert(adjacent_to(addr, sz), "Not adjacent regions");
    if (base() == addr + sz) {
      set_base(addr);
    }
    set_size(size() + sz);
  }

  // Returns 0 if regions overlap; 1 if this region follows rgn;
  //  -1 if this region precedes rgn.
  inline int compare(const VirtualMemoryRegion& rgn) const {
    if (overlap_region(rgn.base(), rgn.size())) {
      return 0;
    } else if (base() >= rgn.end()) {
      return 1;
    } else {
      assert(rgn.base() >= end(), "Sanity");
      return -1;
    }
  }

  // Returns true if regions overlap, false otherwise.
  inline bool equals(const VirtualMemoryRegion& rgn) const {
    return compare(rgn) == 0;
  }

 protected:
  void set_base(address base) {
    assert(base != nullptr, "Sanity check");
    _base_address = base;
  }

  void set_size(size_t  size) {
    assert(size > 0, "Sanity check");
    _size = size;
  }
};


class CommittedMemoryRegion : public VirtualMemoryRegion {
 private:
  NativeCallStack  _stack;

 public:
  CommittedMemoryRegion(address addr, size_t size, const NativeCallStack& stack) :
    VirtualMemoryRegion(addr, size), _stack(stack) { }

  inline void set_call_stack(const NativeCallStack& stack) { _stack = stack; }
  inline const NativeCallStack* call_stack() const         { return &_stack; }
};


typedef LinkedListIterator<CommittedMemoryRegion> CommittedRegionIterator;

int compare_committed_region(const CommittedMemoryRegion&, const CommittedMemoryRegion&);
class ReservedMemoryRegion : public VirtualMemoryRegion {
 private:
  SortedLinkedList<CommittedMemoryRegion, compare_committed_region>
    _committed_regions;

  NativeCallStack  _stack;
  MEMFLAGS         _flag;

 public:
  ReservedMemoryRegion(address base, size_t size, const NativeCallStack& stack,
    MEMFLAGS flag = mtNone) :
    VirtualMemoryRegion(base, size), _stack(stack), _flag(flag) { }


  ReservedMemoryRegion(address base, size_t size) :
    VirtualMemoryRegion(base, size), _stack(NativeCallStack::empty_stack()), _flag(mtNone) { }

  // Copy constructor
  ReservedMemoryRegion(const ReservedMemoryRegion& rr) :
    VirtualMemoryRegion(rr.base(), rr.size()) {
    *this = rr;
  }

  inline void  set_call_stack(const NativeCallStack& stack) { _stack = stack; }
  inline const NativeCallStack* call_stack() const          { return &_stack;  }

  void  set_flag(MEMFLAGS flag);
  inline MEMFLAGS flag() const            { return _flag;  }

  // uncommitted thread stack bottom, above guard pages if there is any.
  address thread_stack_uncommitted_bottom() const;

  bool    add_committed_region(address addr, size_t size, const NativeCallStack& stack);
  bool    remove_uncommitted_region(address addr, size_t size);

  size_t  committed_size() const;

  // move committed regions that higher than specified address to
  // the new region
  void    move_committed_regions(address addr, ReservedMemoryRegion& rgn);

  CommittedRegionIterator iterate_committed_regions() const {
    return CommittedRegionIterator(_committed_regions.head());
  }

  ReservedMemoryRegion& operator= (const ReservedMemoryRegion& other) {
    set_base(other.base());
    set_size(other.size());

    _stack =         *other.call_stack();
    _flag  =         other.flag();

    CommittedRegionIterator itr = other.iterate_committed_regions();
    const CommittedMemoryRegion* rgn = itr.next();
    while (rgn != nullptr) {
      _committed_regions.add(*rgn);
      rgn = itr.next();
    }

    return *this;
  }

  const char* flag_name() const { return NMTUtil::flag_to_name(_flag); }

 private:
  // The committed region contains the uncommitted region, subtract the uncommitted
  // region from this committed region
  bool remove_uncommitted_region(LinkedListNode<CommittedMemoryRegion>* node,
    address addr, size_t sz);

  bool add_committed_region(const CommittedMemoryRegion& rgn) {
    assert(rgn.base() != nullptr, "Invalid base address");
    assert(size() > 0, "Invalid size");
    return _committed_regions.add(rgn) != nullptr;
  }
};

int compare_reserved_region_base(const ReservedMemoryRegion& r1, const ReservedMemoryRegion& r2);

class VirtualMemoryWalker : public StackObj {
 public:
   virtual bool do_allocation_site(const ReservedMemoryRegion* rgn) { return false; }
};

class NewVirtualMemoryTracker {
   using Id = uint32_t;
public:
   static constexpr Id process_space = 0; // Special-case when mapping virtual memory onto itself
   struct PhysicalMemorySpace {
    Id id; // Uniquely identifies the device
    const char* name; // Provided by user for pretty-printing
    static Id unique_id; // Next unique device = 1
    static Id next_unique() {
      return unique_id++;
    }
   };

  struct Range {
    address start;
    size_t size;
    Range(address start, size_t size)
    : start(start), size(size) {}
    address end() {
      return start + size;
    }
  };
  struct TrackedRange : public Range {
    size_t offset; // What offset (address) into the PhysicalMemorySpace does it point to?
    int stack_idx; // From whence did this happen?
    TrackedRange(address start = 0, size_t size = 0, size_t offset = 0, int stack_idx = -1)
    :  Range(start, size),
        offset(offset),
        stack_idx(stack_idx) {
    }
    TrackedRange(const TrackedRange& rng) = default;
    TrackedRange& operator=(const TrackedRange& rng) {
      this->start = rng.start;
      this->size = rng.size;
      this->offset = rng.offset;
      this->stack_idx = rng.stack_idx;
      return *this;
    }
    TrackedRange(TrackedRange&& rng)
    : Range(rng.start, rng.size), offset(rng.offset), stack_idx(rng.stack_idx) {}
   };

private:

  // Split the range to_split by removing to_remove from it, storing the remaining parts in out.
  // Returns true if an overlap was found and will fill the out array with at most 2 elements.
  // The integer pointed to by len will be  set to the number of resulting TrackedRanges.
  static bool overlap_of(TrackedRange to_split, Range to_remove, TrackedRange* out, int* len) {
    // to_split enclosed entirely by to_remove -- nothing is left
    if (to_split.start >= to_remove.start && to_split.end() <= to_remove.end()) {
      *len = 0;
      return true;
    }
    // to_remove enclosed entirely by to_split -- we end up with two ranges and a hole in the middle
    if (to_remove.start >= to_split.start && to_remove.end() < to_split.end()) {
      *len = 2;
      address left_start = to_split.start;
      size_t left_size = static_cast<size_t>(to_remove.start - to_split.start);
      size_t left_offset = to_split.offset;
      out[0] = TrackedRange{left_start, left_size , to_split.offset, to_split.stack_idx};
      address right_start = to_remove.end();
      size_t right_size = static_cast<size_t>((to_split.start + to_split.size) - right_start);
      size_t right_offset =
          to_split.offset +
          (right_start - left_start); // How far along have we traversed into our offset?
      out[1] = TrackedRange{right_start, right_size, right_offset, to_split.stack_idx};
      return true;
    }
    // Overlap from the left -- We end up with one region on the right
    if (to_remove.start < to_split.start && to_remove.end() > to_split.start &&
        to_remove.end() < to_split.end()) {
      *len = 1;
      out[0] = TrackedRange{to_remove.end(), static_cast<size_t>(to_split.end() - to_remove.end()), to_split.offset + (to_remove.end() - to_split.start), to_split.stack_idx};
      return true;
    }
    // overlap from the right
    if (to_split.start < to_remove.start && to_split.end() > to_remove.start &&
        to_split.end() < to_remove.end()) {
      *len = 1;
      out[0] = TrackedRange{to_split.start, static_cast<size_t>(to_remove.start - to_split.start), to_split.offset, to_split.stack_idx};
      return true;
    }
    // No overlap at all
    *len = 0;
    return false;
   }

  using RegionStorage = GrowableArrayCHeap<TrackedRange, mtNMT>;
  static GrowableArrayCHeap<RegionStorage*, mtNMT>* reserved_regions;
  static GrowableArrayCHeap<RegionStorage, mtNMT>* committed_regions;
  // TODO: What to do about the stacks? Seems like we need a ref-counting hashtable for them.
  static GrowableArrayCHeap<NativeCallStack, mtNMT>* all_the_stacks;
public:
  static PhysicalMemorySpace virt_mem;
  static void init() {
    reserved_regions = new GrowableArrayCHeap<RegionStorage*, mtNMT>{5};
    committed_regions = new GrowableArrayCHeap<RegionStorage, mtNMT>{5};
    all_the_stacks = new GrowableArrayCHeap<NativeCallStack, mtNMT>{};
    virt_mem = register_space();
  }

   static PhysicalMemorySpace register_space() {
     const  PhysicalMemorySpace next_space = PhysicalMemorySpace{PhysicalMemorySpace::next_unique()};
     reserved_regions->reserve(next_space.id);
     committed_regions->reserve(next_space.id);
     RegionStorage* memregs = NEW_C_HEAP_ARRAY(RegionStorage, mt_number_of_types, mtNMT);
     for (int memflag = 0; memflag < mt_number_of_types; memflag++) {
       ::new(&memregs[memflag]) RegionStorage{8};
     }
     reserved_regions->at_put_grow(next_space.id, memregs);
     return next_space;
  }
  static void add_view_into_space(address base_addr, size_t size,
                                  const PhysicalMemorySpace space, size_t offset,
                                  MEMFLAGS flag, const NativeCallStack& stack) {
    int idx = all_the_stacks->length();
    all_the_stacks->push(stack);
    reserved_regions->at(space.id)[static_cast<int>(flag)].push(TrackedRange{base_addr, size, offset, idx});
  }
  static void remove_view_into_space(const PhysicalMemorySpace space, address base_addr, size_t size) {
    Range range_to_remove{base_addr, size};
    RegionStorage* arr = reserved_regions->at(space.id);
    for (int memflag = 0; memflag < mt_number_of_types; memflag++) {
      RegionStorage* range_array = &arr[memflag];
      for (int i = 0; i < range_array->length(); i++) {
        TrackedRange out[2];
        int len;
        bool has_overlap = overlap_of(range_array->at(i), range_to_remove, out, &len);
        if (has_overlap) {
          // Delete old region.
          range_array->delete_at(i);
          for (int j = 0; j < len; j++) {
            range_array->push(out[j]);
          }
        }
      }
    }
  }
  static void remove_all_views_into_space(const PhysicalMemorySpace space) {
    RegionStorage* arr = reserved_regions->at(space.id);
    for (int i = 0; i < mt_number_of_types; i++) {
      arr[i].clear_and_deallocate();
    }
  }

  static void set_view_region_type(const PhysicalMemorySpace space, address base_addr, MEMFLAGS flag) {
    RegionStorage* arr = reserved_regions->at(space.id);
    // Must be mtNone
    RegionStorage& range_array = arr[static_cast<int>(mtNone)];
    for (int i = 0; i < range_array.length(); i++) {
      TrackedRange* r = range_array.adr_at(i);
      if (r->start == base_addr) {
        // Found it. Make a copy and push to correct flag
        arr[static_cast<int>(flag)].push(*r);
        // Delete old one.
        range_array.delete_at(i);
        // Assume exactly one match.
        return;
      }
    }
    // TODO: We should assert that one must be found.
  }

  static void commit_memory_into_space(const PhysicalMemorySpace space, size_t offset, size_t size,  const NativeCallStack& stack) {
    int idx = all_the_stacks->length();
    all_the_stacks->push(stack);
    // Points at itself
    committed_regions->at_ref_grow(space.id, [](RegionStorage* p) -> void {
      ::new (p) RegionStorage{};
    }).push(TrackedRange{(address)offset, size, offset, idx});
  }

  static void uncommit_memory_into_space(const PhysicalMemorySpace space, size_t offset, size_t size) {
    Range range_to_remove{(address)offset, size};
    RegionStorage& commits = committed_regions->at(space.id);
    for (int i = 0; i < commits.length(); i++) {
      TrackedRange out[2];
      int len;
      bool has_overlap = overlap_of(commits.at(i), range_to_remove, out, &len);
      if (has_overlap) {
        // Delete old region.
        commits.delete_at(i);
        for (int j = 0; j < len; j++) {
          commits.push(out[j]);
        }
      }
    }
  }

  static void report(outputStream* output = tty) {
    auto print_virtual_memory_region = [&](const char* type, address base, size_t size) -> void {
      const char* scale = "KB";
      output->print("[" PTR_FORMAT " - " PTR_FORMAT "] %s " SIZE_FORMAT "%s", p2i(base),
                    p2i(base + size), type, NMTUtil::amount_in_scale(size, 1024), scale); // TODO: hardcoded scale
    };
    for (uint32_t space_id = 0; space_id < static_cast<uint32_t>(reserved_regions->length()); space_id++) {
      output->print_cr("Virtual memory map of space %d:", space_id);
      auto comm_regs = committed_regions->adr_at(space_id);
      comm_regs->sort([](TrackedRange* a, TrackedRange* b) -> int {
        return a->start - b->start;
      });
      RegionStorage* memflag_regs = reserved_regions->at(space_id);
      for (int memflag = 0; memflag < mt_number_of_types; memflag++) {
        int cursor = 0; // Cursor into comm_regs -- since both are sorted we'll be OK
        RegionStorage& res_regs = memflag_regs[memflag];
        res_regs.sort([](TrackedRange* a, TrackedRange* b) -> int {
          return a->start - b->start;
        });
        for (int rr = 0; rr < res_regs.length(); rr++) {
          TrackedRange rng = res_regs.at(rr);
          auto stack = all_the_stacks->adr_at(rng.stack_idx);
          output->print_cr(" "); // Imitating
          print_virtual_memory_region("reserved", rng.start, rng.size);
          output->print(" for %s", NMTUtil::flag_to_name((MEMFLAGS)memflag));
          if (stack->is_empty()) {
            output->print_cr(" ");
          } else {
            output->print_cr(" from");
            stack->print_on(output, 4);
          }
          while (cursor < comm_regs->length()) {
            TrackedRange comrng = comm_regs->at(cursor);
            stack = all_the_stacks.adr_at(comrng.stack_idx);
            // If the committed range has any overlap with the reserved memory range, then we print it
            // This is a bit too coarse-grained perhaps, but it doesn't invent new ranges.
            // In the future we might want to split the range when printing so that exactly the covered area
            // is printed. This condition would probably stay, however
            if (comrng.start >= (address)rng.offset && // If the committed range starts within the reserved range
                comrng.start < ((address)rng.offset + rng.size) || // Or

                comrng.start + comrng.size >= (address)rng.offset && // the committed range ends within the reserved range
                comrng.start + comrng.size < (address)rng.offset + rng.size) {
              print_virtual_memory_region("committed", comrng.start, comrng.size);
              if (stack->is_empty()) {
                output->print_cr(" ");
              } else {
                output->print_cr(" from");
                stack->print_on(output, 12);
              }
              cursor++;
            } else {
              // Not inside and both arrays are sorted =>
              // we can break
              break;
            }
          }
          output->set_indentation(0);
        }
      }
    }
  }
};

// Main class called from MemTracker to track virtual memory allocations, commits and releases.
class VirtualMemoryTracker : AllStatic {
  friend class VirtualMemoryTrackerTest;
  friend class CommittedVirtualMemoryTest;

 public:
  static bool initialize(NMT_TrackingLevel level);

  static bool add_reserved_region (address base_addr, size_t size, const NativeCallStack& stack, MEMFLAGS flag = mtNone);

  static bool add_committed_region      (address base_addr, size_t size, const NativeCallStack& stack);
  static bool remove_uncommitted_region (address base_addr, size_t size);
  static bool remove_released_region    (address base_addr, size_t size);
  static bool remove_released_region    (ReservedMemoryRegion* rgn);
  static void set_reserved_region_type  (address addr, MEMFLAGS flag);

  // Given an existing memory mapping registered with NMT, split the mapping in
  //  two. The newly created two mappings will be registered under the call
  //  stack and the memory flags of the original section.
  static bool split_reserved_region(address addr, size_t size, size_t split);

  // Walk virtual memory data structure for creating baseline, etc.
  static bool walk_virtual_memory(VirtualMemoryWalker* walker);

  // If p is contained within a known memory region, print information about it to the
  // given stream and return true; false otherwise.
  static bool print_containing_region(const void* p, outputStream* st);

  // Snapshot current thread stacks
  static void snapshot_thread_stacks();

 private:
  static SortedLinkedList<ReservedMemoryRegion, compare_reserved_region_base>* _reserved_regions;
};

#endif // SHARE_SERVICES_VIRTUALMEMORYTRACKER_HPP

