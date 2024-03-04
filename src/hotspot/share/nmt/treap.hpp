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

#include "utilities/globalDefinitions.hpp"
#include "memory/allocation.hpp"
#include "runtime/os.hpp"
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
template<typename K, typename V, int(*CMP)(K,K)>
class TreapNode {
  friend class VirtualMemoryView;

  template<typename KK, typename VV, int(*CMPP)(KK,KK)> // Just need unique names
  friend class TreapCHeap;

  template<typename METADATA, bool(*EquivalentMetadata)(const METADATA&, const METADATA&)>
  friend class VMATree;

  uint64_t priority;
  K key;
  V value;
  using Nd = TreapNode<K,V,CMP>;
  TreapNode<K, V, CMP>* left;
  TreapNode<K, V, CMP>* right;

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
  : priority(p), key(k), value(v), left(nullptr), right(nullptr) {
  }

  const V& val() {
    return value;
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

  template<typename MakeNode>
  static Nd* upsert(Nd* head, const K& k, const V& v, MakeNode make_node) {
    // (LEQ_k, GT_k)
    pair split = Nd::split(head, k);
    Nd* found = find(split.left, k);
    if (found != nullptr) {
      // Already exists, update value.
      found->value = v;
      return merge(split.left, split.right);
    }
    // Doesn't exist, make node
    Nd* node = make_node(k, v);
    // merge(merge(LEQ_k, EQ_k), GT_k)
    return merge(merge(split.left, node), split.right);
  }

  template<typename Free>
  static Nd* remove(Nd *head, const K& k, Free free) {
    // (LEQ_k, GT_k)
    pair fstSplit = split(head, k, LEQ);
    // (LT_k, GEQ_k) == (LT_k, EQ_k) since it's from LEQ_k and keys are unique.
    pair sndSplit = split(fstSplit.left, k, LT);

    if (sndSplit.right != nullptr) {
      // The key k existed, we delete it.
      free(sndSplit.right);
    }
    // Merge together everything
    return merge(sndSplit.left, fstSplit.right);
  }
};

template<typename K, typename V, int(*CMP)(K,K)>
class TreapCHeap {
  template<typename METADATA, bool(*EquivalentMetadata)(const METADATA&, const METADATA&)>
  friend class VMATree;
  using CTreap = TreapNode<K, V, CMP>;
  CTreap* tree;
  uint64_t prng_seed;
public:
  TreapCHeap(uint64_t seed = 1234) : tree(nullptr), prng_seed(seed) {
  }

  uint64_t prng_next() {
    // Taken directly off of JFRPrng
    static const uint64_t PrngMult = 0x5DEECE66DLL;
    static const uint64_t PrngAdd = 0xB;
    static const uint64_t PrngModPower = 48;
    static const uint64_t PrngModMask = (static_cast<uint64_t>(1) << PrngModPower) - 1;
    prng_seed = (PrngMult * prng_seed + PrngAdd) & PrngModMask;
    return prng_seed;
  }

  void upsert(const K& k, const V& v) {
    tree = CTreap::upsert(tree, k, v, [&](const K& k, const V& v) {
      uint64_t rand = this->prng_next();
      void* place = os::malloc(sizeof(CTreap), mtNMT);
      new (place) CTreap(k, v, rand);
      return (CTreap*)place;
    });
  }

  void remove(const K& k) {
    tree = CTreap::remove(tree, k, [](void* ptr) {
      os::free(ptr);
    });
  }
};

#endif //SHARE_NMT_TREAP_HPP
