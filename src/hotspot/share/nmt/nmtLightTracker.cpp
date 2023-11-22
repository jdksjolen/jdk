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

#include "nmt/nmtLightTracker.hpp"

 NMTLightTracker::NMTSummary NMTLightTracker::_summary[(unsigned)MEMFLAGS::mt_number_of_types];


void NMTLightTracker::initialize() {

}
void NMTLightTracker::make_summary(const NMTRecord& rec) {
  if (rec.arena) {
    if (rec._new) {
      Atomic::inc(&_summary[rec.flag].arena.count);
      Atomic::add(&_summary[rec.flag].arena.size, rec.size);
    }
    if (rec._free || rec.resize) {
      Atomic::dec(&_summary[rec.flag].arena.count);
      Atomic::sub(&_summary[rec.flag].arena.size, rec.size);
    }
    return;
  }
  if (rec.malloc) {
    if (rec._new) {
      Atomic::inc(&_summary[rec.flag].malloc.count);
      Atomic::add(&_summary[rec.flag].malloc.size, rec.size);
    }
    if (rec._free) {
      Atomic::dec(&_summary[rec.flag].malloc.count);
      Atomic::sub(&_summary[rec.flag].malloc.size, rec.size);
    }
    return;
  }
  if (rec.commit) {
    Atomic::add(&_summary[rec.flag].commit, rec.size);
    return;
  }
  if (rec.uncommit) {
    if (_summary[rec.flag].commit >= rec.size)
      Atomic::sub(&_summary[rec.flag].commit, rec.size);
    return;
  }
  if (rec.reserve) {
    Atomic::add(&_summary[rec.flag].reserve, rec.size);
    return;
  }
  if (rec.release) {
    if (_summary[rec.flag].reserve >= rec.size)
      Atomic::sub(&_summary[rec.flag].reserve, rec.size);
  }
}
