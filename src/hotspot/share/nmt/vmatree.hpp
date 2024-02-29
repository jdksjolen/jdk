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

#ifndef SHARE_NMT_VMATREE_HPP
#define SHARE_NMT_VMATREE_HPP

#include "memory/resourceArea.hpp"
#include "utilities/globalDefinitions.hpp"
#include "nmt/treap.hpp"
#include "runtime/os.hpp"
#include "utilities/growableArray.hpp"

int addr_cmp(size_t a, size_t b);
uint64_t prng_next();

void* node_malloc(size_t x);

template<typename METADATA>
class VMATree {
public:
  struct State { bool in; bool out; METADATA metadata; };
  using VTreap = TreapNode<size_t, State, addr_cmp, prng_next, node_malloc, os::free>;
  VTreap* tree;
  VMATree()
  : tree(nullptr) {
  }

  void register_mapping(size_t A, size_t B, bool in_use, METADATA& metadata) {
    State stA{false, in_use, metadata};
    State stB{in_use, false, METADATA()};

    // First handle A.
    // Find first node LEQ A
    VTreap* le_n = nullptr;
    { // LE search
      VTreap* head = tree;
      while (head != nullptr) {
        int cmp_r = addr_cmp(head->key, A);
        if (cmp_r == 0) { // Exact match
          le_n = head;
        }
        if (cmp_r < 0) {
          // Found a match, try to find a better one.
          le_n = head;
          head = head->right;
        } else {
          head = head->left;
        }
      }
    }
    if (le_n == nullptr) {
      // NO match.
      if (stA.in == stA.out) {
        // nothing to do.
      } else {
        // Add new node.
        tree = VTreap::upsert(tree, A, stA);
      }
    } else {
      // Unless we know better, let B's outgoing state be the outgoing state of the node at or preceding A.
      stB.out = le_n->value.out;

      // Direct address match.
      if (le_n->key == A) {
        // Take over in state from old address.
        stA.in = le_n->value.in;

        // But we may now be able to merge two regions:
        // If the node's old state matches the new, it becomes a noop. That happens, for example,
        //  when expanding a committed area: commit [x1, x2); ... commit [x2, x3)
        //  and the result should be a larger area, [x1..x3). In that case, the middle node (x2)
        //  is not needed anymore.
        // So we just remove the old node.
        if (stA.in == stA.out) {
          // invalidates le_n
          tree = VTreap::remove(tree, le_n->key);
        } else {
          // re-use existing node
          le_n->value = stA;
        }
      } else {
        // The address must be smaller.

        // We add a new node, but only if there would be a state change. If there would not be a
        // state change, we just omit the node.
        // That happens, for example, when reserving within an already reserved region.
        stA.in = le_n->value.out; // .. and the region's prior state is the incoming state
        if (stA.in == stA.out) {
          // Nothing to do.
        } else {
          // Add new node.
          tree = VTreap::upsert(tree, A, stA);
        }
      }
    }

    // Now we handle B.
    // We first search all nodes that are between A and B. All of these nodes
    // need to be deleted. The last node before B determines B's outgoing state.
    // If there is no node between A and B, its A's incoming state.
    GrowableArrayCHeap<size_t, mtNMT> to_be_deleted;
    bool B_needs_insert = true;

    // Find all nodes between (A, B] and record their addresses. Also update B's
    // outgoing state.
    { // Iterate over each node who is larger than A
    GrowableArrayCHeap<VTreap*, mtNMT> to_visit;
      to_visit.push(tree);
      VTreap* head = nullptr;
      while (!to_visit.is_empty()) {
        head = to_visit.top();
        to_visit.pop();
        if (head == nullptr) continue;

        int cmp_A = addr_cmp(head->key, A);
        int cmp_B = addr_cmp(head->key, B);
        if (cmp_B > 0) {
          // B's node didnt exist, we must match the next node's in value.
          if (B_needs_insert) {
            stB.out = head->value.in;
          }
          break; // Exit the loop.
        } else if (cmp_A > 0 && cmp_B <= 0) {
          stB.out = head->value.out;
          if (cmp_B < 0) {
            // Delete all nodes preceding B.
            to_be_deleted.push(head->key);
          } else if (cmp_B == 0) {
            // Re-purpose B node, unless it would result in a noop node, in
            // which case delete old node at B.
            if (stB.in == stB.out) {
              to_be_deleted.push(B);
            } else {
              head->value = stB;
            }
            B_needs_insert = false;
          } else { /* Unreachable */}

          // Go both left and right.
          to_visit.push(head->left);
          to_visit.push(head->right);
        } else if (cmp_A < 0) {
          // Don't do it.
          // Go right.
          to_visit.push(head->right);
        }
      }
    }
    // Insert B node if needed
    if (B_needs_insert && !(stB.in == stB.out)) {
      tree = VTreap::upsert(tree, B, stB);
    }

    // Finally, if needed, delete all nodes between (A, B)
    while (to_be_deleted.length() > 0) {
      const size_t delete_me = to_be_deleted.top();
      to_be_deleted.pop();
      tree = VTreap::remove(tree, delete_me);
    }
  }

  void register_new_mapping(size_t from, size_t to, METADATA& mdata) {
    register_mapping(from, to, true, mdata);
  }
  void register_unmapping(size_t from, size_t to, METADATA& mdata) {
    register_mapping(from, to, false, mdata);
  }

  // Visit all nodes between [from, to) and call f on them.
  template<typename F>
  void visit(size_t from, size_t to, F f) {
    ResourceArea area(mtNMT);
    ResourceMark rm(&area);
    GrowableArray<VTreap*> to_visit(&area, 16, 0, nullptr);
    to_visit.push(tree);
    VTreap* head = nullptr;
    while (!to_visit.is_empty()) {
      head = to_visit.top();
      to_visit.pop();
      if (head == nullptr) continue;

      int cmp_from = addr_cmp(head->key, from);
      int cmp_to = addr_cmp(head->key, to);
      if (cmp_to >= 0) {
        return;
      }
      if (cmp_from >= 0) {
        if (cmp_to < 0) {
          f(head);
        }
        to_visit.push(head->left);
        to_visit.push(head->right);
      } else {
        to_visit.push(head->right);
      }
    }
  }
};

#endif
