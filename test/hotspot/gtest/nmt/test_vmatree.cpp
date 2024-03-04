#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "nmt/vmatree.hpp"
#include "nmt/memTracker.hpp"
#include "runtime/os.hpp"
#include "unittest.hpp"

struct Nothing {
  static bool equals(const Nothing& a, const Nothing& b) {
    return true;
  }
};
TEST_VM(VMATreeTest, Basics) {
  Nothing nothing;
  using Tree = VMATree<Nothing, Nothing::equals>;
  using Node = Tree::VTreap;
  {
    Tree tree;
    tree.reserve_mapping(0, 100, nothing);
    tree.reserve_mapping(100, 100, nothing);
    int count = 0;
    tree.visit(0, 300, [&](Node* x) {
      count++;
    });
    EXPECT_EQ(count, 2) << "Expected two nodes: one for the start of the range and one for the end.";
  }
  {
    Tree tree;
    tree.reserve_mapping(0, 100, nothing);
    tree.release_mapping(0, 100);
    tree.visit(0, 300, [&](Node* x) {
      Tree::State v = x->val();
      EXPECT_TRUE(v.in == Tree::InOut::Released && v.out == Tree::InOut::Released) << "No in/out should be reserved when all ranges have been removed";
    });
  }
}
