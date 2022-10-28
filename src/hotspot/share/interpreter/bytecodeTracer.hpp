/*
 * Copyright (c) 1997, 2022, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_INTERPRETER_BYTECODETRACER_HPP
#define SHARE_INTERPRETER_BYTECODETRACER_HPP

#include "memory/allStatic.hpp"
#include "oops/method.hpp"
#include "utilities/ostream.hpp"

// The BytecodeTracer is a helper class used by the interpreter for run-time
// bytecode tracing. If bytecode tracing is turned on, trace() will be called
// for each bytecode.
// class BytecodeTracer is used by TraceBytecodes option and PrintMethodData

class methodHandle;

class BytecodePrinter {
 private:
  // %%% This field is not GC-ed, and so can contain garbage
  // between critical sections.  Use only pointer-comparison
  // operations on the pointer, except within a critical section.
  // (Also, ensure that occasional false positives are benign.)
  Method* _current_method;
  bool      _is_wide;
  Bytecodes::Code _code;
  address   _next_pc;                // current decoding position

  void      align()                  { _next_pc = align_up(_next_pc, sizeof(jint)); }
  int       get_byte()               { return *(jbyte*) _next_pc++; }  // signed
  short     get_short()              { short i=Bytes::get_Java_u2(_next_pc); _next_pc+=2; return i; }
  int       get_int()                { int i=Bytes::get_Java_u4(_next_pc); _next_pc+=4; return i; }

  int       get_index_u1()           { return *(address)_next_pc++; }
  int       get_index_u2()           { int i=Bytes::get_Java_u2(_next_pc); _next_pc+=2; return i; }
  int       get_index_u1_cpcache()   { return get_index_u1() + ConstantPool::CPCACHE_INDEX_TAG; }
  int       get_index_u2_cpcache()   { int i=Bytes::get_native_u2(_next_pc); _next_pc+=2; return i + ConstantPool::CPCACHE_INDEX_TAG; }
  int       get_index_u4()           { int i=Bytes::get_native_u4(_next_pc); _next_pc+=4; return i; }
  int       get_index_special()      { return (is_wide()) ? get_index_u2() : get_index_u1(); }
  Method* method()                 { return _current_method; }
  bool      is_wide()                { return _is_wide; }
  Bytecodes::Code raw_code()         { return Bytecodes::Code(_code); }


  bool      check_index(int i, int& cp_index, outputStream* st = tty);
  bool      check_cp_cache_index(int i, int& cp_index, outputStream* st = tty);
  bool      check_obj_index(int i, int& cp_index, outputStream* st = tty);
  bool      check_invokedynamic_index(int i, int& cp_index, outputStream* st = tty);
  void      print_constant(int i, outputStream* st = tty);
  void      print_field_or_method(int i, outputStream* st = tty);
  void      print_field_or_method(int orig_i, int i, outputStream* st = tty);
  void      print_attributes(int bci, outputStream* st = tty);
  void      bytecode_epilog(int bci, outputStream* st = tty);

 public:
  BytecodePrinter() {
    _is_wide = false;
    _code = Bytecodes::_illegal;
  }

  // This method is called while executing the raw bytecodes, so none of
  // the adjustments that BytecodeStream performs applies.
  void trace(const methodHandle& method, address bcp, uintptr_t tos, uintptr_t tos2, outputStream* st);

  // Used for Method*::print_codes().  The input bcp comes from
  // BytecodeStream, which will skip wide bytecodes.
  void trace(const methodHandle& method, address bcp, outputStream* st);
};

class BytecodeTracer {
 private:
  BytecodePrinter _closure;

 public:
  BytecodeTracer() : _closure() {};
  void             trace(const methodHandle& method, address bcp, uintptr_t tos, uintptr_t tos2, outputStream* st = tty);
  void             trace(const methodHandle& method, address bcp, outputStream* st = tty);
};

#endif // SHARE_INTERPRETER_BYTECODETRACER_HPP
