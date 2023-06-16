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

class BufferBlob;
class AbstractCompiler;
class ciEnv;
class CompileThread;
class CompileLog;
class CompileTask;
class CompileQueue;
class CompilerCounters;
class IdealGraphPrinter;
class JVMCIEnv;
class JVMCIPrimitiveArray;

// A thread used for Compilation.
class CompilerThread : public JavaThread {
  friend class VMStructs;
 private:
  CompilerCounters* _counters;

  ciEnv*                _env;
  CompileLog*           _log;
  CompileTask* volatile _task;  // print_threads_compiling can read this concurrently.
  CompileQueue*         _queue;
  BufferBlob*           _buffer_blob;

  AbstractCompiler*     _compiler;
  TimeStamp             _idle_time;
public:
  ContiguousProvider _resource_area_memory; // Backing memory for the ResourceArea
  ContiguousProvider _compiler_memory; // Backing memory for the Compile class.
  // Memory for the various resource areas allocated for a compilation.
  ContiguousProvider _matcher_memory; // Backing memory for the Matcher class.
  ContiguousProvider _chaitin_memory1; // Backing memory for the Chaitin class.
  ContiguousProvider _chaitin_memory2; // Backing memory for the Chaitin class.
  ContiguousProvider _cfg_memory; // Backing memory for the Chaitin class.
  // Backing memory for the Node arenas
  ContiguousProvider _narena_mem_one;
  ContiguousProvider _narena_mem_two;
  void reset_memory(bool force = false) {
    // This backs a ResourceArea, so always kill everything.
    _matcher_memory.reset_full();
    _chaitin_memory1.reset_full();
    _chaitin_memory2.reset_full();
    _cfg_memory.reset_full();
    if (force) {
      // Minimize memory usage -- we're probably idling
      _compiler_memory.reset_full();
      _narena_mem_one.reset_full();
      _narena_mem_two.reset_full();
    } else {
      // Only keep the amount of memory that the last compilation needed
      _compiler_memory.reset_full(_compiler_memory.used());
      _narena_mem_one.reset_full(_narena_mem_one.used());
      _narena_mem_two.reset_full(_narena_mem_two.used());
    }
  }

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

  virtual bool can_call_java() const;

  // Hide native compiler threads from external view.
  bool is_hidden_from_external_view() const      { return !can_call_java(); }

  void set_compiler(AbstractCompiler* c)         { _compiler = c; }
  AbstractCompiler* compiler() const             { return _compiler; }

  CompileQueue* queue()        const             { return _queue; }
  CompilerCounters* counters() const             { return _counters; }

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
