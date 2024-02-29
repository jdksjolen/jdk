/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
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
#include "precompiled.hpp"
#include "nmt/memTracker.hpp"
#include "nmt/threadStackTracker.hpp"
#include "nmt/virtualMemoryTracker.hpp"
#include "nmt/virtualMemoryView.hpp"
#include "runtime/os.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"
#include <new>

int VirtualMemoryView::PhysicalMemorySpace::unique_id = 0;
VirtualMemoryView::PhysicalMemorySpace VirtualMemoryView::Interface::_heap{};
VirtualMemoryView* VirtualMemoryView::Interface::_instance = nullptr;
GrowableArrayCHeap<const char*, mtNMT>* VirtualMemoryView::Interface::_names = nullptr;

VirtualMemoryView::VirtualMemoryView(bool is_detailed_mode)
: _virt_mem{}, _stack_storage{is_detailed_mode} {}

void VirtualMemoryView::Interface::initialize(bool is_detailed_mode) {
  _instance = (VirtualMemoryView*)os::malloc(sizeof(VirtualMemoryView), mtNMT);
  ::new (_instance) VirtualMemoryView(is_detailed_mode);;
  _names = new GrowableArrayCHeap<const char*, mtNMT>{};
  _heap = register_space("Heap");
}

VirtualMemoryView::PhysicalMemorySpace VirtualMemoryView::Interface::register_space(const char* descriptive_name) {
  const PhysicalMemorySpace next_space = PhysicalMemorySpace{PhysicalMemorySpace::next_unique()};
  // These are allocated just to be copied for at_put_grow.
  VirtualMemorySnapshot to_copy_snapshot{};
  return next_space;
}

void VirtualMemoryView::reserve_memory(PhysicalMemorySpace space, address base_addr, size_t size,
                                       MEMFLAGS flag, const NativeCallStack& stack) {
  NativeCallStackStorage::StackIndex idx = _stack_storage.push(stack);
  MetadataReserved md(idx, flag, space, (size_t)base_addr);
  _virt_mem.reserved_regions.register_new_mapping((size_t)base_addr, (size_t)base_addr+size, md);
}
void VirtualMemoryView::release_memory(address base_addr, size_t size) {
  // No-op metadata
  MetadataReserved md((size_t)base_addr);
  _virt_mem.reserved_regions.register_unmapping((size_t)base_addr, (size_t)base_addr+size, md);
}
void VirtualMemoryView::commit_memory_into_space(const PhysicalMemorySpace space, address offset,
                                                 size_t size, const NativeCallStack& stack) {
  NativeCallStackStorage::StackIndex idx = _stack_storage.push(stack);
  MetadataCommitted md{idx};
  _virt_mem.committed_regions.at(space.id).register_new_mapping((size_t)offset, (size_t)offset+size, md);
}
void VirtualMemoryView::uncommit_memory_into_space(const PhysicalMemorySpace& space, address offset,
                                                   size_t size) {
  MetadataCommitted md;
  _virt_mem.committed_regions.at(space.id).register_unmapping((size_t)offset, (size_t)offset+size, md);
}
void VirtualMemoryView::add_view_into_space(const PhysicalMemorySpace& space, address base_addr,
                                            size_t size, address offset, MEMFLAGS flag,
                                            const NativeCallStack& stack) {
  NativeCallStackStorage::StackIndex idx = _stack_storage.push(stack);
  MetadataReserved md{idx, flag, space, (size_t)offset};
  _virt_mem.reserved_regions.register_new_mapping((size_t)base_addr, (size_t)base_addr+size, md);
}
void VirtualMemoryView::remove_view_into_space(const PhysicalMemorySpace& space, address base_addr,
                                               size_t size) {
  MetadataReserved md;
  _virt_mem.reserved_regions.register_unmapping((size_t)base_addr, (size_t)base_addr+size, md);
}


void VirtualMemoryView::Interface::reserve_memory(address base_addr, size_t size, MEMFLAGS flag,
                                                  const NativeCallStack& stack) {
  //_instance->reserve_memory(_heap, base_addr, size, flag, stack);
}
void VirtualMemoryView::Interface::release_memory(address base_addr, size_t size) {
  //_instance->release_memory(base_addr, size);
}
void VirtualMemoryView::Interface::commit_memory(address base_addr, size_t size,
                                                 const NativeCallStack& stack) {
  //_instance->commit_memory_into_space(_heap, base_addr, size, stack);
}
void VirtualMemoryView::Interface::uncommit_memory(address base_addr, size_t size) {
  //_instance->uncommit_memory_into_space(_heap, base_addr, size);
}
void VirtualMemoryView::Interface::add_view_into_space(const PhysicalMemorySpace& space,
                                                       address base_addr, size_t size,
                                                       address offset, MEMFLAGS flag,
                                                       const NativeCallStack& stack) {
}
void VirtualMemoryView::Interface::remove_view_into_space(const PhysicalMemorySpace& space,
                                                          address base_addr, size_t size) {
}
void VirtualMemoryView::Interface::commit_memory_into_space(const PhysicalMemorySpace& space,
                                                            address offset, size_t size,
                                                            const NativeCallStack& stack) {
}
void VirtualMemoryView::Interface::uncommit_memory_into_space(const PhysicalMemorySpace& space,
                                                              address offset, size_t size) {
}
void VirtualMemoryView::Interface::report(VirtualMemory& mem, outputStream* output, size_t scale) {
}
void VirtualMemoryView::Interface::compute_summary_snapshot(VirtualMemory& vmem) {
}
