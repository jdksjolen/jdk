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

#include "nmt/vmatree.hpp"

void* node_malloc(size_t x) {
  return os::malloc(x, mtNMT);
}
uint64_t prng_seed = 12345;
uint64_t prng_next() {
  static const uint64_t PrngMult = 0x5DEECE66DLL;
  static const uint64_t PrngAdd = 0xB;
  static const uint64_t PrngModPower = 48;
  static const uint64_t PrngModMask = (static_cast<uint64_t>(1) << PrngModPower) - 1;
  prng_seed =  (PrngMult * prng_seed + PrngAdd) & PrngModMask;
  return prng_seed;
}
int addr_cmp(size_t a, size_t b) {
  if (a < b) return -1;
  if (a == b) return 0;
  if (a > b) return 1;
  else {
    // Can't happen
    return -1337;
  }
}
