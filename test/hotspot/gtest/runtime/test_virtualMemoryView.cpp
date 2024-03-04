#include "precompiled.hpp"

#include "unittest.hpp"
#include "memory/virtualspace.hpp"
#include "nmt/memTracker.hpp"
#include "nmt/virtualMemoryTracker.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

#include <stdio.h>

class VirtualMemoryViewTest : public testing::Test {
  using VMV = VirtualMemoryView;
public:
  static VirtualMemoryView vmv;
  static VMV::PhysicalMemorySpace space;

  VirtualMemoryViewTest() {
    space = VirtualMemoryView::PhysicalMemorySpace{0};
  }

  static address addr(size_t x) {
    return (address)x;
  }
  static void r(size_t address, size_t size, MEMFLAGS f = mtTest, const NativeCallStack& stack = CURRENT_PC) {
    vmv.reserve_memory(space, addr(address), size, f, stack);
  }
  static void c(size_t address, size_t size, const NativeCallStack& stack = CURRENT_PC) {
    vmv.commit_memory_into_space(space, addr(address), size, stack);
  }

  static void v(size_t address, size_t size, size_t offs, MEMFLAGS flag = mtTest, const NativeCallStack& stack = CURRENT_PC) {
    vmv.add_mapping_into_space(space, addr(address), size, addr(offs), flag, stack);
  }

  static void test_summary_computation() {
    r(0, 100);
    r(100, 200);
    r(200, 300);
  }

  static void test_reserve_commit_release() {
  }
};
VirtualMemoryView VirtualMemoryViewTest::vmv{false /*is_detailed_mode*/};
VirtualMemoryView::PhysicalMemorySpace VirtualMemoryViewTest::space{0}; // Doesn't matter, will make new one during construction

TEST_VM_F(VirtualMemoryViewTest, TestReserveCommitRelease) {
  VirtualMemoryViewTest::test_reserve_commit_release();
}

TEST_VM_F(VirtualMemoryViewTest, TestSummaryComputation) {
  VirtualMemoryViewTest::test_summary_computation();
}
