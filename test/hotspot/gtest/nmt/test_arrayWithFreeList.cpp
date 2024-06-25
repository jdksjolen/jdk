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

 */

#include "precompiled.hpp"
#include "unittest.hpp"
#include "nmt/arrayWithFreeList.hpp"

using A = ArrayWithFreeList<int, mtTest>;

class ArrayWithFreeListTest  : public testing::Test {
};

TEST_VM_F(ArrayWithFreeListTest, FreeingShouldReuseMemory) {
  A alloc;
  A::I i = alloc.allocate(1);
  int* x = &alloc.at(i);
  alloc.deallocate(i);
  i = alloc.allocate(1);
  int* y = &alloc.at(i);
  EXPECT_EQ(x, y);
}

TEST_VM_F(ArrayWithFreeListTest, FreeingInTheMiddleWorks) {
  A alloc;
  A::I i0 = alloc.allocate(0);
  A::I i1 = alloc.allocate(0);
  A::I i2 = alloc.allocate(0);
  int* p1 = &alloc.at(i1);
  alloc.deallocate(i1);
  A::I i3 = alloc.allocate(0);
  EXPECT_EQ(p1, &alloc.at(i3));
}
