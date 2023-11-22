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
#ifndef NMT_LIGHT_TRACKER_HPP
#define NMT_LIGHT_TRACKER_HPP
#include "runtime/atomic.hpp"
#include "nmt/nmtCommon.hpp"
#include "nmt/mallocTracker.hpp"
#include "nmt/virtualMemoryTracker.hpp"

struct NMTRecord {
  unsigned malloc:1;
  unsigned arena:1;
  unsigned _new:1;
  unsigned _free:1;
  unsigned resize:1;
  unsigned reserve:1;
  unsigned commit:1;
  unsigned uncommit:1;
  unsigned release:1;
  unsigned flag:7;
  size_t size;
  NMTRecord(size_t s, MEMFLAGS f) { reset(); size = s; flag = (unsigned)f; }
  void reset() {
    *(int*)this = 0;
    size = 0;
  }
};

class NMTLightTracker : public AllStatic {
 private:
  struct NMTSummary {
    struct NMTMeasures {
      size_t count, size;
    } malloc, arena;
    size_t reserve, commit;
  };
  static NMTSummary _summary[(unsigned)MEMFLAGS::mt_number_of_types];
 public:
  static void initialize();
  static void record_malloc(size_t size, MEMFLAGS flag) {
    NMTRecord rec(size, flag);
    rec.malloc = true;
    rec._new = true;
    make_summary(rec);
  }
  static void record_free(size_t size, MEMFLAGS flag) {
    NMTRecord rec(size, flag);
    rec.malloc = true;
    rec._free = true;
    make_summary(rec);
  }
  static void record_new_arena(MEMFLAGS flag) {
    NMTRecord rec(0, flag);
    rec.arena = true;
    rec._new = true;
    make_summary(rec);
  }
  static void record_arena_free(MEMFLAGS flag) {
    NMTRecord rec(0, flag);
    rec.arena = true;
    rec._free = true;
    make_summary(rec);
  }
  static void record_arena_size_change(ssize_t diff, MEMFLAGS flag) {
    NMTRecord rec(diff, flag);
    rec.arena = true;
    if (diff < 0) {
      rec.size = -diff;
      rec.resize = true;
      make_summary(rec);
    } else {
      rec.resize = false;
      rec._new = true;
      make_summary(rec);
    }
  }
  static void record_virtual_memory_reserve(size_t size, MEMFLAGS flag) {
    if (flag == mtNone) return;
    NMTRecord rec(size, flag);
    rec.reserve = true;
    make_summary(rec);
  }
  static void record_virtual_memory_reserve_and_commit(size_t size, MEMFLAGS flag) {
    if (flag == mtNone) return;
    NMTRecord rec(size, flag);
    rec.reserve = true;
    make_summary(rec);

    rec.reserve = false;
    rec.commit = true;
    make_summary(rec);
  }
  static void record_virtual_memory_commit(size_t size, MEMFLAGS flag) {
    if (flag == mtNone) return;
    NMTRecord rec(size, flag);
    rec.commit = true;
    make_summary(rec);
  }
  static void record_virtual_memory_split_reserved(size_t size, size_t split) {
    NMTRecord rec0(split, mtClassShared);  rec0.reserve = true; make_summary(rec0);
    NMTRecord rec1(split, mtClassShared);  rec1.commit  = true; make_summary(rec1);

    NMTRecord rec2(size - split, mtClass); rec2.reserve = true; make_summary(rec2);
    NMTRecord rec3(size - split, mtClass); rec3.commit  = true; make_summary(rec3);

    NMTRecord rec4(size, mtNone);          rec4.uncommit = true; make_summary(rec4);
    NMTRecord rec5(size, mtNone);          rec5.release  = true; make_summary(rec5);
  }
  static void record_virtual_memory_type(size_t size, MEMFLAGS flag) { }
  static void record_virtual_memory_uncommit(size_t size, MEMFLAGS flag) {
    NMTRecord rec(size, flag);
    rec.uncommit = true;
    make_summary(rec);
  }
  static void record_virtual_memory_release(size_t size, MEMFLAGS flag) {
    NMTRecord rec(size, flag);
    rec.release = true;
    make_summary(rec);
  }
  static void make_summary(const NMTRecord& rec);
  static void malloc_snapshot(MallocMemorySnapshot* s) {
    size_t malloc_total = 0;
    size_t arena_total = 0;

    for (int index = 0; index < mt_number_of_types; index ++) {
      malloc_total += _summary[index].malloc.size;
      arena_total += _summary[index].arena.size;
      s->_malloc[index].malloc_counter_lv()->allocate(_summary[index].malloc.size);
      s->_malloc[index].arena_counter_lv()->allocate(_summary[index].arena.size);
    }
    int chunk_idx = NMTUtil::flag_to_index(mtChunk);
    s->_malloc[chunk_idx].record_free(arena_total);
    s->_all_mallocs.allocate(malloc_total);
    s->_all_mallocs.deallocate(arena_total);
  }
  static void virtual_memory_snapshot(VirtualMemorySnapshot* s) {
    for (int index = 0; index < mt_number_of_types; index ++) {
      MEMFLAGS flag = NMTUtil::index_to_flag(index);
      s->by_type(flag)->reserve_memory(_summary[index].reserve);
      if (_summary[index].commit <= _summary[index].reserve)
        s->by_type(flag)->commit_memory(_summary[index].commit);
    }
  }
};

#endif // NMT_LIGHT_TRACKER_HPP