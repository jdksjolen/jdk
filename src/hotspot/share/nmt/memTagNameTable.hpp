/*
 * Copyright (c) 2025, Oracle and/or its affiliates. All rights reserved.
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

// Dual-mapping tag to name and name to tag
// where strings are malloc-allocated
#include "memory/allocation.hpp"
#include "memory/allocation.inline.hpp"
#include "mutex_posix.hpp"
#include "nmt/memTag.hpp"
#include "nmt/nmtCommon.hpp"
#include "runtime/threadCritical.hpp"
#include "utilities/debug.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/resourceHash.hpp"
#include <stdint.h>

#ifndef SHARE_NMT_MEMTAGNAMETABLE_HPP
#define SHARE_NMT_MEMTAGNAMETABLE_HPP

struct MemTagNameTable : public CHeapObjBase {
  static constexpr const auto nr_of_buckets = 4096;
  using StringRef = int32_t;

  struct NameToTagTable {
    using EntryRef = int32_t;
    constexpr static const EntryRef Nil = -1;

    struct Entry {
      StringRef name;
      MemTag tag;
      EntryRef next;

      Entry(StringRef name, MemTag tag, EntryRef next)
        : name(name),
          tag(tag),
          next(next) {
      }
      Entry() : next(Nil) {}
    };

    GrowableArrayCHeap<Entry, mtNMT> entries;
    const int table_size;
    EntryRef* table;
    GrowableArrayCHeap<char, mtNMT>& names;

    NameToTagTable(GrowableArrayCHeap<char, mtNMT>& names)
    : entries(), table_size(nr_of_buckets), table(nullptr), names(names) {
      table = NEW_C_HEAP_ARRAY(EntryRef, table_size, mtNMT);
      for (int i = 0; i < table_size; i++) {
        table[i] = Nil;
      }
    }

    // string hash taken from libadt and made worse!
    int string_hash(const char* t) {
      char c;
      int k = 0;
      int32_t sum = 0;
      const char* s = (const char*)t;

      while (((c = *s++) != '\0')) {
        c = (c << 1) + 1;
        sum += c + (c << (k++ % 6));
      }
      return abs((int)((sum + 261) >> 1));
    }

    StringRef put(MemTag tag, const char* name) {
      int bucket = string_hash(name) % table_size;
      EntryRef link = table[bucket];
      while (link != Nil) {
        Entry e = entries.at(link);
        if (strcmp(names.adr_at(e.name), name) == 0) {
          return e.name;
        }
      }
      int i = names.length();
      const size_t name_len = strlen(name);
      names.at_grow(i + name_len, 0);
      memcpy(names.adr_at(i), name, name_len);
      Entry nentry(i, tag, table[bucket]);
      entries.push(nentry);
      table[bucket] = entries.length() - 1;
      return i;
    }

    MemTag get(const char* name) {
      int bucket = string_hash(name) % table_size;
      EntryRef link = table[bucket];
      while (link != Nil) {
        Entry e = entries.at(link);
        if (strcmp(names.adr_at(e.name), name) == 0) {
          return e.tag;
        }
      }
      return mtNone;
    }
  };

  GrowableArrayCHeap<char, mtNMT> names;
  GrowableArrayCHeap<StringRef, mtNMT> tag_to_name;
  NameToTagTable name_to_tag;

  MemTagNameTable() : tag_to_name(), name_to_tag(names) {
    // Make all old-style tags undefined
    for (int i = 0; i < mt_number_of_tags; i++) {
      tag_to_name.push(0);
    }
  }

  MemTag make_tag(const char* name) {
    MemTag mt = get(name);
    if (mt == MemTag::mtNone) {
      uint32_t len = tag_to_name.length();
      put_when_absent(len, name);
      return (MemTag)len;
    } else {
      return mt;
    }
  }
  void put_when_absent(uint32_t tag, const char* name) {
    uint32_t len = tag_to_name.length();
    if (len <= tag) {
      // Entry missing.
      StringRef name_ref = name_to_tag.put((MemTag)tag, name);
      StringRef& name_place = tag_to_name.at_grow(tag);
      name_place = name_ref;
    }
  }

  void put(MemTag tag, const char* name) {
    this->put_when_absent((uint32_t)tag, name);
  }

  const char* get(uint32_t tag) {
    StringRef name_ref = tag_to_name.at(tag);
    return names.adr_at(name_ref);
  }

  const char* get(MemTag tag) {
    return get((uint32_t)tag);
  }

  MemTag get(const char* name) {
    return name_to_tag.get(name);
  }

  struct Instance : public AllStatic {
    static MemTagNameTable* _instance;

    static void initialize() {
      ThreadCritical tc;
      _instance = new (mtNMT) MemTagNameTable;
    }

    static MemTag get(const char* name) {
      ThreadCritical tc;
      return _instance->get(name);
    }

    static const char* get(MemTag tag) {
      ThreadCritical tc;
      const char* ret = _instance->get(tag); return ret;
    }

    static MemTag make_tag(const char* name) {
      ThreadCritical tc;
      return _instance->make_tag(name);
    }
  };
};

struct MemTagFactory : public AllStatic {
  static MemTag tag(const char* name) {
    MemTag mt = NMTUtil::string_to_mem_tag(name);
    if (mt != mtNone) {
      return mt;
    }
    return MemTagNameTable::Instance::make_tag(name);
  }
};

#endif
