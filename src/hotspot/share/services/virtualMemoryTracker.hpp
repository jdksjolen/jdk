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

#include "logging/logStream.hpp"
#include "memory/allocation.hpp"
#include "memory/metaspace.hpp" // For MetadataType
#include "memory/metaspaceStats.hpp"
#include "services/allocationSite.hpp"
#include "services/nmtCommon.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/linkedlist.hpp"
#include "utilities/nativeCallStack.hpp"
#include "utilities/ostream.hpp"
#include "logging/log.hpp"

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

/*
  Some ideas of potential things that can improve the situation:
  1. We currently only compress committed regions.
     This is not as trivial for the reserved regions, you need to check that both the physical_address and address are adjacent
     to each other and also test the flag.
  2. Insertion sort is online, stable, and fast on almost sorted input.
     It might be worth doing it explicitly?
  3. There is a special but slower online algorithm for reporting offset ranges. It's probably worth it to optimize the common case here.
  4. Switch offset to address.
  5. Optimize memory usage for the committed memory down to 24 bytes.
 */
class NewVirtualMemoryTracker {
   using Id = uint32_t;
public:
   struct PhysicalMemorySpace {
    Id id; // Uniquely identifies the device
    const char* name; // Provided by user for pretty-printing
    static Id unique_id;
    static Id next_unique() {
      return unique_id++;
    }
   };

  // Some memory range
  struct Range {
    address start;
    size_t size;
    Range(address start, size_t size)
    : start(start), size(size) {}
    address end() {
      return start + size;
    }
  };
  // Add tracking information
  struct TrackedRange : public Range { // Currently unused, but can be used by the old API and all committed memory
    int stack_idx; // From whence did this happen?
    MEMFLAGS flag; // What flag does it have? -- Might be mtNone
    TrackedRange(address start, size_t size, int stack_idx, MEMFLAGS flag) :
      Range(start, size), stack_idx(stack_idx), flag(flag) {}
  };
  // Give it the possibility of being offset
  struct TrackedOffsetRange : public TrackedRange {
    size_t physical_address;
    TrackedOffsetRange(address start = 0, size_t size = 0, size_t physical_address = 0, int stack_idx = -1, MEMFLAGS flag = mtNone)
      :  TrackedRange(start, size, stack_idx, flag),
         physical_address(physical_address) {}
    TrackedOffsetRange(const TrackedOffsetRange& rng) = default;
    TrackedOffsetRange& operator=(const TrackedOffsetRange& rng) {
      this->start = rng.start;
      this->size = rng.size;
      this->physical_address = rng.physical_address;
      this->stack_idx = rng.stack_idx;
      this->flag = rng.flag;
      return *this;
    }
    TrackedOffsetRange(TrackedOffsetRange&& rng)
      : TrackedRange(rng.start, rng.size, rng.stack_idx, rng.flag), physical_address(rng.physical_address) {}

    size_t physical_end() {
      return physical_address + size;
    }
  };

private:

  // Split the range to_split by removing to_remove from it, storing the remaining parts in out.
  // Returns true if an overlap was found and will fill the out array with at most 2 elements.
  // The integer pointed to by len will be  set to the number of resulting TrackedRanges.
  // The physical address is managed appropriately for the out array.
  enum class OverlappingResult {
    NoOverlap,
    EntirelyEnclosed,
    SplitInMiddle,
    ShortenedFromLeft,
    ShortenedFromRight,
  };
  static OverlappingResult overlap_of(TrackedOffsetRange to_split, Range to_remove, TrackedOffsetRange* out, int* len) {
    const auto a = to_split.start; const auto b = to_split.end();
    const auto c = to_remove.start; const auto d = to_remove.end();
    /*
      to_split enclosed entirely by to_remove -- nothing is left
      Also handles the case where they are exactly the same, still has the same result.
         a  b
       | |  | | => None.
       c      d
     */
    if (a >= c && b <= d) {
      *len = 0;
      return OverlappingResult::EntirelyEnclosed;
    }
    // to_remove enclosed entirely by to_split -- we end up with two ranges and a hole in the middle
    /*
       a      b    a c   d b
       | |  | | => | | , | |
         c  d
     */
    if (c > a && d < b) {
      *len = 2;
      address left_start = a;
      size_t left_size = static_cast<size_t>(c - a);
      size_t left_offset = to_split.physical_address;
      out[0] = TrackedOffsetRange{left_start, left_size , to_split.physical_address, to_split.stack_idx, to_split.flag};
      address right_start = d;
      size_t right_size = static_cast<size_t>((a + to_split.size) - right_start);
      size_t right_offset =
          to_split.physical_address +
          (right_start - left_start); // How far along have we traversed into our offset?
      out[1] = TrackedOffsetRange{right_start, right_size, right_offset, to_split.stack_idx, to_split.flag};
      return OverlappingResult::SplitInMiddle;
    }
    // Overlap from the left -- We end up with one region on the right
    /*
        a    b    d  b
      | | |  | => |  |
      c   d
     */
    if (c <= a && d > a && d < b) {
      *len = 1;
      out[0] = TrackedOffsetRange{d, static_cast<size_t>(b - d),
                            to_split.physical_address + (d - a), to_split.stack_idx, to_split.flag};
      return OverlappingResult::ShortenedFromLeft;
    }
    // Overlap from the right
    /*
      a   b       a  c
      | | |  | => |  |
        c    d
     */
    if (a < c && c < b && b <= d) {
      *len = 1;
      out[0] = TrackedOffsetRange{a, static_cast<size_t>(c - a), to_split.physical_address, to_split.stack_idx, to_split.flag};
      return OverlappingResult::ShortenedFromRight;
    }
    // No overlap at all
    *len = 0;
    return OverlappingResult::NoOverlap;
   }

  // TODO: Optimize for regular reserved/committed memory just like we have it in current VMT
  using RegionStorage = GrowableArrayCHeap<TrackedOffsetRange, mtNMT>;
  static GrowableArrayCHeap<RegionStorage, mtNMT>* reserved_regions;
  static GrowableArrayCHeap<RegionStorage, mtNMT>* committed_regions;

  static constexpr const int static_stack_size = 256;
  static GrowableArrayCHeap<NativeCallStack, mtNMT>* all_the_stacks;
  static int push_stack(const NativeCallStack& stack) {
    int len = all_the_stacks->length();
    int idx = stack.calculate_hash() % static_stack_size;
    if (len < idx) {
      all_the_stacks->at_put_grow(idx, stack);
      return idx;
    }
    // Exists and already there? No need for double storage
    if (all_the_stacks->at(idx).equals(stack)) {
      return idx;
    }
    all_the_stacks->push(stack);
    return len;
  }

  // Utilities
  static bool overlaps(Range a, Range b) {
    return MAX2(b.start, a.start) < MIN2(b.end(), a.end());
  }
  // Pre-condition: ranges is sorted in a left-aligned fashion
  // That is: (a,b) comes before (c,d) if a <= c
  // Merges the ranges into a minimal sequence, taking into account that two ranges can only be merged if:
  // 1. Their NativeCallStacks are the same
  // 2. Their starts align correctly
  static RegionStorage merge_committed(RegionStorage& ranges) {
    RegionStorage merged_ranges;
    auto rlen = ranges.length();
    if (rlen == 0) return merged_ranges;
    int j = 0;
    merged_ranges.push(ranges.at(j));
    for (int i = 1; i < rlen; i++) {
      TrackedOffsetRange& merging_range = merged_ranges.at(j);
      TrackedOffsetRange& potential_range = ranges.at(i);
      if (merging_range.end() >=
              potential_range.start // There's overlap, known because of pre-condition
          && all_the_stacks->at(merging_range.stack_idx)
                 .equals(all_the_stacks->at(potential_range.stack_idx))) {
        // Merge it
        merging_range.size = potential_range.end() - merging_range.start;
      } else {
        j++;
        merged_ranges.push(potential_range);
      }
    }
    return merged_ranges;
  }

  static void sort_regions(RegionStorage& storage) {
    storage.sort([](TrackedOffsetRange* a, TrackedOffsetRange* b) -> int {
      return (a->physical_address > b->physical_address) -
             (a->physical_address < b->physical_address);
    });
  }

public:
  static PhysicalMemorySpace virt_mem;
  static void init() {
    reserved_regions = new GrowableArrayCHeap<RegionStorage, mtNMT>{5};
    committed_regions = new GrowableArrayCHeap<RegionStorage, mtNMT>{5};
    all_the_stacks = new GrowableArrayCHeap<NativeCallStack, mtNMT>{static_stack_size};
    virt_mem = register_space();
  }

   static PhysicalMemorySpace register_space() {
     const  PhysicalMemorySpace next_space = PhysicalMemorySpace{PhysicalMemorySpace::next_unique()};
     reserved_regions->at_ref_grow(next_space.id, [](RegionStorage* p) -> void {
      ::new (p) RegionStorage{128};
     });
     committed_regions->at_ref_grow(next_space.id, [](RegionStorage* p) -> void {
      ::new (p) RegionStorage{128};
     });
     return next_space;
  }
  static void add_view_into_space(const PhysicalMemorySpace& space, address base_addr, size_t size,
                                  size_t offset, MEMFLAGS flag, const NativeCallStack& stack) {
     int stack_idx = push_stack(stack);
     RegionStorage& rngs = reserved_regions->at(space.id);
     if (space.id == virt_mem.id) {
       // In this case we know that we're following the old API. That is, the offset and physical address matches 1:1
       // this is basically trivial?
      rngs.push(TrackedOffsetRange{base_addr, size, offset, stack_idx, flag});
      return;
     }
     // More complicated case -- we need to find overlapping regions and split on them.
     for (int i = 0; i < rngs.length(); i++) {
      TrackedOffsetRange& rng = rngs.at(i);
      TrackedOffsetRange out[2];
      int len;
      OverlappingResult res = overlap_of(rng, Range{base_addr, size}, out, &len);
      if (res == OverlappingResult::NoOverlap) {
        // Do nothing
      } else if (res == OverlappingResult::EntirelyEnclosed) {
        // We replace it.
        rngs.at_put(i, TrackedOffsetRange{base_addr, size, offset, stack_idx, flag});
        for (int j = 0; j < len; j++) {
          rngs.push(out[j]);
        }
      }
     }
  }

  static void remove_view_into_space(const PhysicalMemorySpace& space, address base_addr, size_t size) {
    Range range_to_remove{base_addr, size};
    RegionStorage& range_array = reserved_regions->at(space.id);
    for (int i = 0; i < range_array.length(); i++) {
      TrackedOffsetRange out[2];
      int len;
      bool has_overlap = OverlappingResult::NoOverlap != overlap_of(range_array.at(i), range_to_remove, out, &len);
      if (has_overlap) {
        // Delete old region.
        range_array.delete_at(i);
        // Replace with the remaining ones
        for (int j = 0; j < len; j++) {
          range_array.push(out[j]);
        }
      }
    }
  }

  static void remove_all_views_into_space(const PhysicalMemorySpace& space) {
    reserved_regions->at(space.id).clear_and_deallocate();
  }

  static void set_view_region_type(const PhysicalMemorySpace& space, address base_addr, MEMFLAGS flag) {
    RegionStorage& range_array = reserved_regions->at(space.id);
    for (int i = 0; i < range_array.length(); i++) {
      TrackedOffsetRange* r = range_array.adr_at(i);
      // Must be mtNone
      if (r->start == base_addr && r->flag == mtNone) {
        // Found it. Make a copy and push to correct flag
        TrackedOffsetRange copy{*r};
        copy.flag = flag;
        range_array.push(copy);
        // Delete old one.
        range_array.delete_at(i);
        // Assume exactly one match.
        return;
      }
    }
    // TODO: We should assert that one must be found.
  }

  static void commit_memory_into_space(const PhysicalMemorySpace& space, size_t offset, size_t size,  const NativeCallStack& stack) {
    RegionStorage& crngs = committed_regions->at(space.id);
    // Small optimization: Is the next commit adjacent to the last one? Then we don't need to push.
    // Metaspace does a lot of commits and hits this branch a lot.
    if (crngs.length() > 0) {
      TrackedOffsetRange& crng = crngs.at(crngs.length() - 1);
      if (crng.end() >= (address)offset &&
          all_the_stacks->at(crng.stack_idx).equals(stack)) {
        crng.size = (offset + size) - (size_t)crng.start;
        return;
      }
    }
    // TODO: Are we about to resize the array? Then we can probably get away with doing a sort+merge, and checking if the resize is still necessary.
    int idx = push_stack(stack);
    crngs.push(TrackedOffsetRange{(address)offset, size, offset, idx});
  }

  static void uncommit_memory_into_space(const PhysicalMemorySpace& space, size_t offset, size_t size) {
    Range range_to_remove{(address)offset, size};
    RegionStorage& commits = committed_regions->at(space.id);
    for (int i = 0; i < commits.length(); i++) {
      TrackedOffsetRange out[2];
      int len;
      bool has_overlap = OverlappingResult::NoOverlap != overlap_of(commits.at(i), range_to_remove, out, &len);
      if (has_overlap) {
        // Delete old region.
        commits.delete_at(i);
        for (int j = 0; j < len; j++) {
          commits.push(out[j]);
        }
      }
    }
  }

public:

  /*
    TODOs:
    1. Incorporate SnapshotThreadStackWalker into the code!! That's where our missing committed regions are
    2. 'Double buffer' the merging of the CRs so that no dynamic allocation is done at report time.
  */

  // Report like the old virtual memory reporter does it.
  static void report_virtual_memory_map(outputStream* output = tty) {
    const uint32_t space_id = virt_mem.id;
    auto print_virtual_memory_region = [&](const char* type, address base, size_t size) -> void {
      const char* scale = "KB";
      output->print("[" PTR_FORMAT " - " PTR_FORMAT "] %s " SIZE_FORMAT "%s", p2i(base),
                    p2i(base + size), type, NMTUtil::amount_in_scale(size, 1024), scale); // TODO: hardcoded scale
    };
    output->print_cr("Virtual memory map:");
    sort_regions(committed_regions->at(space_id));
    RegionStorage comm_regs = merge_committed(committed_regions->at(space_id));
    int printed_committed_regions = 0;
    // Cursor into comm_regs. Since both are sorted we only have to do one pass over the committed regions
    int cursor = 0;
    RegionStorage& res_regs = reserved_regions->at(space_id);
    sort_regions(res_regs);
    for (int res_reg_idx = 0; res_reg_idx < res_regs.length(); res_reg_idx++) {
      TrackedOffsetRange& rng = res_regs.at(res_reg_idx);
      NativeCallStack& stack = all_the_stacks->at(rng.stack_idx);
      output->print_cr(" "); // Imitating
      print_virtual_memory_region("reserved", rng.start, rng.size);
      output->print(" for %s", NMTUtil::flag_to_name(rng.flag));
      if (stack.is_empty()) {
        output->print_cr(" ");
      } else {
        output->print_cr(" from");
        stack.print_on(output, 4);
      }
      // Track whether we've started overlapping
      // Any committed region that isn't matched while found_one_overlap is false has no overlapping reserved region.
      while (cursor < comm_regs.length()) {
        TrackedOffsetRange& comrng = comm_regs.at(cursor);
        if (overlaps(Range{(address)rng.physical_address, rng.size}, Range{comrng.start, comrng.size})) {
          NativeCallStack& stack = all_the_stacks->at(comrng.stack_idx);
          output->print("\n\t");
          print_virtual_memory_region("committed", comrng.start, comrng.size);
          if (stack.is_empty()) {
            output->print_cr(" ");
          } else {
            output->print_cr(" from");
            stack.print_on(output, 12);
          }
          printed_committed_regions++;
        } else if (comrng.end() < (address)rng.physical_address) {
          output->print_cr("MISSING CR");
          NativeCallStack& stack = all_the_stacks->at(comrng.stack_idx);
          output->print("\n\t");
          print_virtual_memory_region("committed", comrng.start, comrng.size);
          if (stack.is_empty()) {
            output->print_cr(" ");
          } else {
            output->print_cr(" from");
            stack.print_on(output, 12);
          }
          // This committed region has no reserved region
        } else {
          // We've stopped seeing overlaps for this range, so we can now break
          break;
        }
        cursor++;
      }
      output->set_indentation(0);
    }
    output->print_cr("Printed CR:s %d, Total CR:s %d", printed_committed_regions, comm_regs.length());
  }

  // Report all memory maps except the first one. The major difference being that this
  // can have offsets, which the first one doesn't have.
  static void report(outputStream* output) {
    const auto print_committed_memory = [&](TrackedOffsetRange& rgn, RegionStorage& comm_regs) {
      for (int i = 0; i < comm_regs.length(); i++) {
        TrackedOffsetRange& crange = comm_regs.at(i);
        if (overlaps(Range{(address)rgn.physical_address, rgn.size}, Range{crange.start, crange.size})) {
          output->print_cr("Print the CR here");
        }
        // TODO: We don't need to loop over everything here and can break early, but that's an optimization for another day.
      }
    };
    // This is unfortunately broken.
    // The reason that this is broken is that the time-of-add matters.
    // So we either need to add a time component to the offset range *or* we need to perform a lot more work
    // for the regions
    for (Id space_id = virt_mem.id+1; space_id < PhysicalMemorySpace::unique_id; space_id++) {
      RegionStorage& res_regs = reserved_regions->at(space_id);
      RegionStorage& com_regs = committed_regions->at(space_id);
      sort_regions(res_regs);
      sort_regions(com_regs);
      for (int res_reg_idx = 0; res_reg_idx < res_regs.length(); res_reg_idx++) {
        TrackedOffsetRange rgn = res_regs.at(res_reg_idx);
        int res_cursor = res_reg_idx+1; // off-by-1
        while (res_cursor < res_regs.length()) {
          TrackedOffsetRange& pot_overlap = res_regs.at(res_cursor);
          TrackedOffsetRange out[2]; int len;
          OverlappingResult overlap = overlap_of(rgn, {pot_overlap.start, pot_overlap.size}, out, &len);
          if (overlap == OverlappingResult::NoOverlap) {
            output->print_cr("Print rgn here!");
            for (int i = 0; i < com_regs.length(); i++) {
              // Fin
            }
            break;
          } else if (overlap == OverlappingResult::EntirelyEnclosed) {
            // They are exactly the same -- just print one and skip 
          }
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

