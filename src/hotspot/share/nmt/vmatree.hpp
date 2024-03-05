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

template<typename METADATA, bool(*EquivalentMetadata)(const METADATA&, const METADATA&)>
class VMATree {
public:
  enum class InOut {
    Reserved,
    Committed,
    Released
  };
  struct State {
    InOut in; // Previous node active
    InOut out; // This node active with metadata
    METADATA metadata;
  };
  bool is_noop(State st) {
    return st.in == st.out;
  }

  using VTreap = TreapNode<size_t, State, addr_cmp>;
  TreapCHeap<size_t, State, addr_cmp> tree;
  VMATree()
  : tree() {
  }

  // TODO: Account for MEMFLAGS.
  struct SummaryDiff {
    int64_t reserve;
    int64_t commit;
  };

  template<typename Merge>
  SummaryDiff register_mapping(size_t A, size_t B, InOut state, METADATA& metadata, Merge merge) {
    State stA{InOut::Released, state, metadata};
    // Ends do not need any METADATA.
    State stB{state, InOut::Released, METADATA()};

    // First handle A.
    // Find first node LEQ A
    VTreap* leqA_n = nullptr;
    { // LE search
      VTreap* head = tree.tree;
      while (head != nullptr) {
        int cmp_r = addr_cmp(head->key(), A);
        if (cmp_r == 0) { // Exact match
          leqA_n = head;
        }
        if (cmp_r < 0) {
          // Found a match, try to find a better one.
          leqA_n = head;
          head = head->right;
        } else {
          head = head->left;
        }
      }
    }
    if (leqA_n == nullptr) {
      // No match.
      if (is_noop(stA)) {
        // nothing to do.
      } else {
        // Add new node.
        tree.upsert(A, stA);
      }
    } else {
      // Unless we know better, let B's outgoing state be the outgoing state of the node at or preceding A.
      // Consider the case where the found node is the start of a region enclosing [A,B)
      // We must also ineherit the metadata now.
      stB.out = leqA_n->val().out;
      stB.metadata = leqA_n->val().metadata;

      // Direct address match.
      if (leqA_n->key() == A) {
        // Take over in state from old address.
        stA.in = leqA_n->val().in;

        // But we may now be able to merge two regions:
        // If the node's old state matches the new, it becomes a noop. That happens, for example,
        // when expanding a committed area: commit [x1, A); ... commit [A, x3)
        // and the result should be a larger area, [x1, x3). In that case, the middle node (A and le_n)
        // is not needed anymore. So we just remove the old node.
        // We can only do this merge if the metadata is considered equivalent.
        if (is_noop(stA) && EquivalentMetadata(stA.metadata, leqA_n->val().metadata)) {
          // invalidates le_n
          tree.remove(leqA_n->key());
        } else {
          // If the state is not matching then we have different operations, such as:
          // reserve [x1, A); ... commit [A, x2)
          // Or we have diffing metadata, then we re-use the existing out node, overwriting its old metadata.
          // TODO: Accept a merge strategy for these cases. In commit_memory we want the memory flag to be kept, for example, but not in release_memory.
          stA.metadata = merge(stA.metadata, leqA_n->_value.metadata);
          leqA_n->_value = stA;
        }
      } else {
        // The address must be smaller.

        // We add a new node, but only if there would be a state change. If there would not be a
        // state change, we just omit the node.
        // That happens, for example, when reserving within an already reserved region with identical metadata.
        stA.in = leqA_n->val().out; // .. and the region's prior state is the incoming state
        if (is_noop(stA) && EquivalentMetadata(stA.metadata, leqA_n->val().metadata)) {
          // Nothing to do.
        } else {
          // Add new node.
          tree.upsert(A, stA);
        }
      }
    }

    // Now we handle B.
    // We first search all nodes that are between A and B. All of these nodes
    // need to be deleted and summary accounted for. The last node before B determines B's outgoing state.
    // If there is no node between A and B, its A's incoming state.
    struct SizeType {
      size_t address;
      InOut in;
      InOut out;
    };
    GrowableArrayCHeap<SizeType, mtNMT> to_be_deleted;
    bool B_needs_insert = true;

    // Find all nodes between (A, B] and record their addresses. Also update B's
    // outgoing state.
    { // Iterate over each node which is larger than A
    GrowableArrayCHeap<VTreap*, mtNMT> to_visit;
      to_visit.push(tree.tree);
      VTreap* head = nullptr;
      while (!to_visit.is_empty()) {
        head = to_visit.top();
        to_visit.pop();
        if (head == nullptr) continue;

        int cmp_A = addr_cmp(head->key(), A);
        int cmp_B = addr_cmp(head->key(), B);
        if (cmp_B > 0) {
          // B's node didnt exist, we must match the next node's in value.
          if (B_needs_insert) {
            stB.out = head->val().in;
          }
          break; // Exit the loop.
        } else if (cmp_A > 0 && cmp_B <= 0) {
          stB.out = head->val().out;
          if (cmp_B < 0) {
            // Delete all nodes preceding B.
            to_be_deleted.push({head->key(), head->val().out, head->val().out });
          } else if (cmp_B == 0) {
            // Re-purpose B node, unless it would result in a noop node, in
            // which case delete old node at B.
            if (is_noop(stB) && EquivalentMetadata(stB.metadata, head->val().metadata)) {
              to_be_deleted.push({B, stB.in, stB.out});
            } else {
              // TODO: Implement a metadata merge strategy
              stB.metadata = merge(stB.metadata, head->_value.metadata);
              head->_value = stB;
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
    if (B_needs_insert    && // Was not already inserted
        (!is_noop(stB)     || // The operation is differing Or
         !EquivalentMetadata(stB.metadata, METADATA{})) // The metadata was changed from empty earlier
        ) {
      tree.upsert(B, stB);
    }

    // Finally, if needed, delete all nodes between (A, B)
    // Performing accounting of the changed nodes so that summary accounting can be done online.
    size_t prev = A;
    SummaryDiff diff{0,0};
    while (to_be_deleted.length() > 0) {
      const SizeType delete_me = to_be_deleted.top();
      to_be_deleted.pop();
      tree.remove(delete_me.address);
      if (delete_me.in == InOut::Reserved) {
        diff.reserve -= delete_me.address - prev;
      } else if (delete_me.in == InOut::Committed) {
        diff.commit -= delete_me.address - prev;
        diff.reserve -= delete_me.address - prev;
      }
      prev = delete_me.address;
    }
    if (state == InOut::Reserved) {
      diff.reserve += B-A;
    } else if(state == InOut::Committed) {
      diff.commit += B-A;
      diff.reserve += B-A;
    }
    return diff;
  }

  static METADATA no_merge(METADATA& a, METADATA& b) {
    return a;
  }

  template<typename Merge>
  SummaryDiff reserve_mapping(size_t from, size_t sz, METADATA& metadata, Merge merge_strategy) {
    return register_mapping(from, from + sz, InOut::Reserved, metadata, merge_strategy);
  }
  template<typename Merge>
  SummaryDiff commit_mapping(size_t from, size_t sz, METADATA& metadata, Merge merge_strategy) {
    return register_mapping(from, from + sz, InOut::Committed, metadata, merge_strategy);
  }
  template<typename Merge>
  SummaryDiff release_mapping(size_t from, size_t sz, Merge merge_strategy) {
    METADATA empty;
    return register_mapping(from, from + sz, InOut::Released, empty);
  }

  // Visit all nodes between [from, to) and call f on them.
  template<typename F>
  void visit(size_t from, size_t to, F f) {
    ResourceArea area(mtNMT);
    ResourceMark rm(&area);
    GrowableArray<VTreap*> to_visit(&area, 16, 0, nullptr);
    to_visit.push(tree.tree);
    VTreap* head = nullptr;
    while (!to_visit.is_empty()) {
      head = to_visit.top();
      to_visit.pop();
      if (head == nullptr) continue;

      int cmp_from = addr_cmp(head->key(), from);
      int cmp_to = addr_cmp(head->key(), to);
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
