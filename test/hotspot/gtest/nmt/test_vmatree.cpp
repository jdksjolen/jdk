#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "nmt/vmatree.hpp"
#include "nmt/memTracker.hpp"
#include "runtime/os.hpp"
#include "unittest.hpp"

TEST_VM(VMATreeTest, Basics) {
  struct Nothing{};
  Nothing nothing;
  using Node = VMATree<Nothing>::VTreap;
  {
    VMATree<Nothing> tree;
    tree.register_mapping(0, 100, true, nothing);
    tree.register_mapping(100, 200, true, nothing);
    int count = 0;
    tree.visit(0, 300, [&](Node* x) {
      count++;
    });
    EXPECT_EQ(count, 2) << "Expected two nodes: one for the start of the range and one for the end.";
  }
  {
    VMATree<Nothing> tree;
    tree.register_mapping(0, 100, true, nothing);
    tree.register_mapping(0, 100, false, nothing);
    tree.visit(0, 300, [&](Node* x) {
      VMATree<Nothing>::State v = x->val();
      EXPECT_TRUE(v.in == false && v.out == false) << "No in/out should be true when all ranges have been removed";
    });
  }
}
