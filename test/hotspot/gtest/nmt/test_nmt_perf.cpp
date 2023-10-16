#include "precompiled.hpp"

#include "memory/allocation.hpp"
#include "runtime/os.hpp"
#include "services/mallocTracker.hpp"
#include "services/virtualMemoryTracker.hpp"
#include "unittest.hpp"
#include <chrono>


TEST_VM(NMTPerf, PerfTest) {
  auto time_it = [](const char* prefix, auto it) {
    auto start = std::chrono::high_resolution_clock::now();
    it();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = (end - start);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    tty->print_cr("[%s] Elapsed time: %ld us", prefix, us.count());
  };

  tty->print_cr("Adding 10000 reserved adjacent regions");
  time_it("New", []() {
    address addr = 0x0;
    size_t size = 1024;
    for (int i = 0; i < 10000; i++) {
      NewVirtualMemoryTracker::add_reserved_region(addr, size, CALLER_PC);
      addr += size;
    }
  });
  time_it("Old",[](){
    address addr = 0x0;
    size_t size = 1024;
    for (int i = 0; i < 10000; i++) {
      VirtualMemoryTracker::add_reserved_region(addr, size, CALLER_PC);
      addr += size;
    }
  });

  tty->print_cr("Adding 10000 reserved non-adjacent regions");
    time_it("New", []() {
      address addr = (address)(0x0 + 1024 * 10000 + 1);
    size_t size = 1024;
    for (int i = 0; i < 10000; i++) {
      NewVirtualMemoryTracker::add_reserved_region(addr, size, CALLER_PC);
      addr += size + 1;
    }
  });
  time_it("Old",[](){
    address addr = (address)(0x0 + 1024 * 10000 + 1);
    size_t size = 1024;
    for (int i = 0; i < 10000; i++) {
      VirtualMemoryTracker::add_reserved_region(addr, size, CALLER_PC);
      addr += size + 1;
    }
  });

}
