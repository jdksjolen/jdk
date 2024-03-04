#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "nmt/vmatree.hpp"
#include "nmt/memTracker.hpp"
#include "runtime/os.hpp"
#include "unittest.hpp"

TEST_VM(VMATreeTest, AdjacentAreMerged) {
  struct Nothing{};
  Nothing nothing;
  VMATree<Nothing> tree;
  tree.register_mapping(0, 100, true, nothing);
  tree.register_mapping(100, 200, true, nothing);
  int count = 0;
  tree.visit(0, 300, [&](VMATree<Nothing>::VTreap* x) {
    count++;
  });
  ASSERT_EQ(count, 1);
}
