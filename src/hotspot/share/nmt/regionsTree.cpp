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
#include "precompiled.hpp"
#include "nmt/regionsTree.hpp"

ReservedMemoryRegion RegionsTree::find_reserved_region(address addr) {
    ReservedMemoryRegion rmr;
    auto contain_region = [&](ReservedMemoryRegion& region_in_tree) {
      if (region_in_tree.contain_address(addr)) {
        rmr = region_in_tree;
        return false;
      }
      return true;
    };
    visit_reserved_regions(contain_region);
    return rmr;
}

void RegionsTree::commit_region(address addr, size_t size, VMATree::SummaryDiff& diff, const NativeCallStack& stack) {
  commit_mapping((VMATree::position)addr, size, make_region_data(stack, mtNone), diff, /*use tag inplace*/ true);
}

void RegionsTree::uncommit_region(address addr, size_t size, VMATree::SummaryDiff& diff) {
  uncommit_mapping((VMATree::position)addr, size, make_region_data(NativeCallStack::empty_stack(), mtNone), diff);
}


