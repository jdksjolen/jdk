/*
 * Copyright (c) 2021, 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_COMPILER_COMPILERTHREAD_HPP
#define SHARE_COMPILER_COMPILERTHREAD_HPP

#include "runtime/javaThread.hpp"
#include <sys/mman.h>

class AbstractCompiler;
class ArenaStatCounter;
class BufferBlob;
class ciEnv;
class CompilerThread;
class CompileLog;
class CompileTask;
class CompileQueue;
class CompilerCounters;
class IdealGraphPrinter;

// A thread used for Compilation.
class CompilerThread : public JavaThread {
  friend class VMStructs;
  JVMCI_ONLY(friend class CompilerThreadCanCallJava;)
 private:
  CompilerCounters* _counters;

  ciEnv*                _env;
  CompileLog*           _log;
  CompileTask* volatile _task;  // print_threads_compiling can read this concurrently.
  CompileQueue*         _queue;
  BufferBlob*           _buffer_blob;
  bool                  _can_call_java;

  AbstractCompiler*     _compiler;
  TimeStamp             _idle_time;
public:
  struct CompilerMemory {
    size_t size;
    char* start;
    char* current;
    size_t size_per;
  public:
    CompilerMemory(size_t divided_by, size_t chunk_size)
      : size(4096*M),
        start(nullptr),
        size_per(0) /* Set later */ {
      void* ret = ::mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      assert(ret != MAP_FAILED, "mustn't");
      char* addr = (char*)ret;
      char* aligned_addr = align_up(addr, chunk_size);
      if (aligned_addr != addr) {
        int x = ::munmap(addr, aligned_addr - addr);
        assert(x == 0, "must");
        size -= aligned_addr - addr;
        addr = aligned_addr;
      }
      // Transparent huge pages are unacceptable.
#ifndef MADV_NOHUGEPAGE
#define MADV_NOHUGEPAGE 15
      ::madvise(addr, size, MADV_NOHUGEPAGE);
#undef MADV_NOHUGEPAGE
#else
      ::madvise(addr, size, MADV_NOHUGEPAGE);
#endif

      // Update
      start = addr;
      current = start;
      size_per = align_down(size / divided_by, chunk_size);
      MemTracker::record_virtual_memory_reserve(start, size, CALLER_PC, mtCompiler);
    }

    ContiguousAllocator::MemoryArea next() {
      ContiguousAllocator::MemoryArea ma{current, size_per};
      if (is_aligned(ma.start, 2*M)) {
        ma.start += ContiguousAllocator::get_chunk_size(false);
        ma.size -= ContiguousAllocator::get_chunk_size(false);
      }
      current += size_per;
      return ma;
    }
    ~CompilerMemory() {
      ::munmap(start, size);
      MemTracker::record_virtual_memory_release(start, size);
    }
  };

  CompilerMemory _backing_compiler_memory; // All of the compiler memory
  ContiguousProvider _resource_area_memory; // Backing memory for the ResourceArea
  ContiguousProvider _compiler_memory; // Backing memory for the Compile class.
  // Memory for the various resource areas allocated for a compilation.
  ContiguousProvider _matcher_memory; // Backing memory for the Matcher class.
  ContiguousProvider _chaitin_memory1; // Backing memory for the Chaitin class.
  ContiguousProvider _chaitin_memory2; // Backing memory for the Chaitin class.
  ContiguousProvider _cfg_memory; // Backing memory for phasecfg.
  ContiguousProvider _phaseccp_memory; // Backing memory for phaseccp.
  // Backing memory for the Node arenas
  ContiguousProvider _narena_mem_one;
  ContiguousProvider _narena_mem_two;
  void reset_memory(bool force = false) {
    if (force) {
      // Minimize memory usage -- we're probably idling
      _matcher_memory.reset_full();
      _chaitin_memory1.reset_full();
      _chaitin_memory2.reset_full();
      _phaseccp_memory.reset_full();
      _cfg_memory.reset_full();
      _compiler_memory.reset_full();
      _narena_mem_one.reset_full();
      _narena_mem_two.reset_full();
    } else {
      // Only keep the amount of memory that the last compilation needed
      _matcher_memory.reset_full(_matcher_memory.used());
      _chaitin_memory1.reset_full(_chaitin_memory1.used());
      _chaitin_memory2.reset_full(_chaitin_memory2.used());
      _phaseccp_memory.reset_full(_phaseccp_memory.used());
      _cfg_memory.reset_full(_cfg_memory.used());
      _compiler_memory.reset_full(_compiler_memory.used());
      _narena_mem_one.reset_full(_narena_mem_one.used());
      _narena_mem_two.reset_full(_narena_mem_two.used());
    }
  }
  ArenaStatCounter*     _arena_stat;

 public:

  static CompilerThread* current() {
    return CompilerThread::cast(JavaThread::current());
  }

  static CompilerThread* cast(Thread* t) {
    assert(t->is_Compiler_thread(), "incorrect cast to CompilerThread");
    return static_cast<CompilerThread*>(t);
  }

  CompilerThread(CompileQueue* queue, CompilerCounters* counters);
  ~CompilerThread();

  bool is_Compiler_thread() const                { return true; }

  virtual bool can_call_java() const             { return _can_call_java; }

  // Returns true if this CompilerThread is hidden from JVMTI and FlightRecorder.  C1 and C2 are
  // always hidden but JVMCI compiler threads might be hidden.
  virtual bool is_hidden_from_external_view() const;

  void set_compiler(AbstractCompiler* c);
  AbstractCompiler* compiler() const             { return _compiler; }

  CompileQueue* queue()        const             { return _queue; }
  CompilerCounters* counters() const             { return _counters; }
  ArenaStatCounter* arena_stat() const           { return _arena_stat; }

  // Get/set the thread's compilation environment.
  ciEnv*        env()                            { return _env; }
  void          set_env(ciEnv* env)              { _env = env; }

  BufferBlob*   get_buffer_blob() const          { return _buffer_blob; }
  void          set_buffer_blob(BufferBlob* b)   { _buffer_blob = b; }

  // Get/set the thread's logging information
  CompileLog*   log()                            { return _log; }
  void          init_log(CompileLog* log) {
    // Set once, for good.
    assert(_log == nullptr, "set only once");
    _log = log;
  }

  void start_idle_timer()                        { _idle_time.update(); }
  jlong idle_time_millis() {
    return TimeHelper::counter_to_millis(_idle_time.ticks_since_update());
  }

#ifndef PRODUCT
 private:
  IdealGraphPrinter *_ideal_graph_printer;
 public:
  IdealGraphPrinter *ideal_graph_printer()           { return _ideal_graph_printer; }
  void set_ideal_graph_printer(IdealGraphPrinter *n) { _ideal_graph_printer = n; }
#endif

  // Get/set the thread's current task
  CompileTask* task()                      { return _task; }
  void         set_task(CompileTask* task) { _task = task; }

  static void thread_entry(JavaThread* thread, TRAPS);
};

#endif  // SHARE_COMPILER_COMPILERTHREAD_HPP
