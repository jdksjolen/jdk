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

#ifndef SHARE_NMT_TREAP_HPP
#define SHARE_NMT_TREAP_HPP

#include <stddef.h>
#include <stdint.h>

// A Treap is a self-balanced binary tree where each node is equipped with a
// priority. It adds the invariant that the priority of a parent P is strictly larger
// larger than the priority of its children. When priorities are randomly
// assigned the tree is balanced.
// All operations are defined through merge and split, which are each other's inverse.
// merge(left_treap, right_treap) => treap where left_treap <= right_treap
// split(treap, key) => (left_treap, right_treap)  where left_treap <= right_treap
// Recursion is used in these, but the depth of the call stack is the depth of
// the tree which is O(log n) so we are safe from stack overflow.
// There is an imperative equivalent of these two, if needed. https://www2.hawaii.edu/~nodari/teaching/f19/scribes/notes07.pdf
template<typename K, typename V, int(*CMP)(K,K), uint64_t(*RAND)(), void* (*ALLOC)(size_t), void (*FREE)(void*)>
class TreapNode {
  template<typename METADATA> // Has to mention the
  friend class VMATree;
  friend class VirtualMemoryView;

  uint64_t priority;
  K key;
  V value;
  using Nd = TreapNode<K,V,CMP, RAND, ALLOC, FREE>;
  TreapNode<K, V, CMP, RAND, ALLOC, FREE>* left;
  TreapNode<K, V, CMP, RAND, ALLOC, FREE>* right;
private:
  struct pair {
    Nd* left;
    Nd* right;
  };

  enum SplitMode {
    LT, // <
    LEQ // <=
  };

  // Split tree at head into two trees, SplitMode decides where EQ values go.
  // We have SplitMode because it makes remove() trivial to implement.
  static pair split(Nd* head, const K& key, SplitMode mode = LEQ) {
    if (head == nullptr) {
      return {nullptr, nullptr};
    }
    if ( (CMP(head->key, key) <= 0 && mode == LEQ) ||
         (CMP(head->key, key) < 0 && mode == LT) ) {
      auto p = split(head->right, key, mode);
      head->right = p.left;
      return {head, p.right};
    } else {
      auto p = split(head->left, key, mode);
      head->left = p.right;
      return {p.left, head};
    }
  }

  // Invariant: left is a treap whose keys are LEQ to the keys in right.
  static Nd* merge(Nd* left, Nd* right) {
    if (left == nullptr) return right;
    if (right == nullptr) return left;

    if (left->priority > right->priority) {
      // We need
      //      LEFT
      //         |
      //         RIGHT
      // For the invariant re: priorities to hold.
      left->right = merge(left->right, right);
      return left;
    } else {
      // We need
      //         RIGHT
      //         |
      //      LEFT
      // For the invariant re: priorities to hold.
      right->left = merge(left, right->left);
      return right;
    }
  }

public:
  TreapNode(const K& k, const V& v, uint64_t p)
  : priority(p), key(k), value(v) {
  }

  static Nd* mk_nd(const K& k, const V& v) {
    void* place = ALLOC(sizeof(Nd));
    new (place) Nd(k, v, RAND());
    return (Nd*)place;
  }

  static Nd* find(Nd* node, const K& k) {
    if (node == nullptr) {
      return nullptr;
    }
    if (CMP(node->key, k) == 0) { // EQ
      return node;
    }

    if (CMP(node->key, k) <= 0) { // LEQ
      return find(node->left, k);
    } else {
      return find(node->right, k);
    }
  }

  static Nd* upsert(Nd* head, const K& k, const V& v) {
    // (LEQ_k, GT_k)
    pair split = Nd::split(head, k);
    Nd* found = find(split.left, k);
    if (found != nullptr) {
      // Already exists, update value.
      found->value = v;
      return merge(split.left, split.right);
    }
    // Doesn't exist, make node
    Nd* node = mk_nd(k, v);
    // merge(merge(LEQ_k, EQ_k), GT_k)
    return merge(merge(split.left, node), split.right);
  }

  static Nd* remove(Nd *head, const K &k) {
    // (LEQ_k, GT_k)
    pair fstSplit = split(head, k, LEQ);
    // (LT_k, GEQ_k) == (LT_k, EQ_k) since it's from LEQ_k and keys are unique.
    pair sndSplit = split(fstSplit.left, k, LT);

    if (sndSplit.right != nullptr) {
      // The key k existed, we delete it.
      FREE(sndSplit.right);
    }
    // Merge together everything
    return merge(sndSplit.left, fstSplit.right);
  }

};

#endif //SHARE_NMT_TREAP_HPP
