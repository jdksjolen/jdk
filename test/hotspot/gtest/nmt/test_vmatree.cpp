#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "nmt/vmatree.hpp"
#include "nmt/virtualMemoryView.hpp"
#include "nmt/memTracker.hpp"
#include "runtime/os.hpp"
#include "unittest.hpp"

struct Nothing {
  static bool equals(const Nothing& a, const Nothing& b) {
    return true;
  }
};

TEST_VM(VMATreeTest, EmptyMetadata) {
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

  {
    Tree tree;
    tree.reserve_mapping(0, 100, nothing);
    tree.commit_mapping(0, 50, nothing);

    size_t found[16];
    size_t wanted[3] = {0, 50, 100};
    auto exists = [&](size_t x) {
      for (int i = 0; i < 3; i++) {
        if (wanted[i] == x) return true;
      }
      return false;
    };
    int i = 0;
    tree.visit(0, 300, [&](Node* x) {
      if (i < 16) {
        found[i] = x->key();
      }
      i++;
    });
    ASSERT_EQ(i, 3) << "0 - 50 - 100 nodes expected";
    EXPECT_TRUE(exists(found[0]));
    EXPECT_TRUE(exists(found[1]));
    EXPECT_TRUE(exists(found[2]));
  }
}

TEST_VM(VMATreeTest, VMV) {
  
}
