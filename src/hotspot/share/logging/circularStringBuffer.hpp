/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_LOGGING_CIRCULARSTRINGBUFFER_HPP
#define SHARE_LOGGING_CIRCULARSTRINGBUFFER_HPP

#include "logging/logFileStreamOutput.hpp"
#include "nmt/memTracker.hpp"
#include "runtime/mutex.hpp"
#include "runtime/os.inline.hpp"
#include "runtime/semaphore.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/resourceHash.hpp"

#include <stddef.h>
#include <string.h>


// The CircularBuffer is a struct that provides
// an interface for writing and reading bytes in a circular buffer
// correctly. This indirection is necessary because there are two
// underlying implementations: Linux, and all others.
#ifdef LINUX
#include <sys/mman.h>
#include <unistd.h>
// Implements a circular buffer by using the virtual memory mapping facilities of the OS.
// Specifically, it reserves virtual memory with twice the size of the requested buffer.
// The latter half of this buffer is then mapped back to the start of the first buffer.
// This allows for write_bytes and read_bytes to consist of a single memcpy, as the
// wrap-around is dealt with by the virtual memory system.
struct CircularBuffer {
  FILE* file;
  char* buffer;
  size_t size;

  CircularBuffer(size_t size)
  : size(size) {
    assert(is_aligned(size, os::vm_page_size()), "must be");
    file = tmpfile();
    if (file == nullptr) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
    const int fd = fileno(file);
    if (fd == -1) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
    int ret = ftruncate(fd, size);
    if (ret != 0) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }

    buffer = (char*)mmap(nullptr, size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
    void* mmap_ret = mmap(buffer, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (mmap_ret == MAP_FAILED) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
    mmap_ret = mmap(buffer + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (mmap_ret == MAP_FAILED) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }

    // Success, notify MT.
    MemTracker::record_virtual_memory_reserve(buffer, size, CURRENT_PC, mtLogging);
    MemTracker::record_virtual_memory_commit(buffer, size, CURRENT_PC);
  }

  void write_bytes(size_t at, const char* bytes, size_t size) {
    assert(bytes != nullptr, "must be"); // memcpy(nullptr, nullptr, 0); is UB.
    memcpy(&buffer[at], bytes, size);
  }

  void read_bytes(size_t at, char* out, size_t size) {
    assert(out != nullptr, "must be");
    memcpy(out, &buffer[at], size);
  }

  ~CircularBuffer() {
    munmap(buffer, size * 2);
    fclose(file);
  }
};
#else
// Implements a circular buffer in the way you'd expect: Doing a manual
// wrapping around and two memcpy:s per read and per write.
// This implementation can be replaced with an equivalent of the Linux-type
// implementation on both Mac OS X and Windows.
struct CircularBuffer {
  char* buffer;
  size_t size;

  CircularBuffer(size_t size)
    : size(size) {
    assert(is_aligned(size, os::vm_page_size()), "must be");
    buffer = os::reserve_memory(size, false, mtLogging);
    if (buffer == nullptr) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
    bool ret = os::commit_memory(buffer, size, false);
    if (!ret) {
      vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "Failed to allocate async logging buffer");
    }
  }

  void write_bytes(size_t at, const char* bytes, size_t size) {
    assert(bytes != nullptr, "must be");
    const size_t part1_size = MIN2(size, this->size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(&buffer[at], bytes, part1_size);
    ::memcpy(buffer, &bytes[part1_size], part2_size);
  }

  void read_bytes(size_t at, char* out, size_t size) {
    assert(out != nullptr, "must be");
    const size_t part1_size = MIN2(size, this->size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(out, &buffer[at], part1_size);
    ::memcpy(&out[part1_size], buffer, part2_size);
  }

  ~CircularBuffer() {
    os::release_memory(buffer, size);
  }
};
#endif // LINUX

// The external interface for the async UL mechanism to store and retrieve messages.
class CircularLogBuffer {
  friend class AsyncLogTest;

public:
  // Account for dropped messages using a ResourceHashTable.
  using StatisticsMap = ResourceHashtable<LogFileStreamOutput*, uint32_t, 17, /*table_size*/
                                          AnyObj::C_HEAP, mtLogging>;

  // Messsage is the header of a log line and contains its associated decorations and output.
  // It is directly followed by the c-str of the log line. The log line is padded at the end
  // to ensure correct alignment for the Message. A Message is considered to be a flush token
  // when its output is null.
  //
  // Example layout:
  // ---------------------------------------------
  // |_output|_decorations|"a log line", |pad| <- Message aligned.
  // |_output|_decorations|"yet another",|pad|
  // ...
  // |nullptr|_decorations|"",|pad| <- flush token
  // |<- _pos
  // ---------------------------------------------
  struct Message {
    size_t size; // Size of string following the Message envelope
    LogFileStreamOutput* const output;
    const LogDecorations decorations;
    Message(size_t size, LogFileStreamOutput* output, const LogDecorations decorations)
      : size(size), output(output), decorations(decorations) {
    }

    Message()
      : size(0), output(nullptr), decorations(None) {
    }

    bool is_token() {
      return output == nullptr;
    }
  };

private:
  static const LogDecorations& None;

  // Need to perform accounting of statistics under a separate lock.
  // The statistics table is provided by AsyncLogWriter.
  StatisticsMap& _stats;
  PlatformMonitor& _stats_lock;

  // Can't use a Monitor here as we need a low-level API that can be used without Thread::current().
  PlatformMonitor _read_lock;
  PlatformMonitor _write_lock;
  Semaphore _flush_sem;

  struct StatsLocker : public StackObj {
    CircularLogBuffer* buf;
    StatsLocker(CircularLogBuffer* buf)
      : buf(buf) {
      buf->_stats_lock.lock();
    }
    ~StatsLocker() {
      buf->_stats_lock.unlock();
    }
  };
  struct ReadLocker : public StackObj {
    CircularLogBuffer* buf;
    ReadLocker(CircularLogBuffer* buf) : buf(buf) {
      buf->_read_lock.lock();
    }
    ~ReadLocker() {
      buf->_read_lock.unlock();
    }
  };
  struct WriteLocker : public StackObj {
    CircularLogBuffer* buf;
    WriteLocker(CircularLogBuffer* buf) : buf(buf) {
      buf->_write_lock.lock();
    }
    ~WriteLocker() {
      buf->_write_lock.unlock();
    }
  };
  // Opaque circular mapping of our buffer.
  CircularBuffer _buffer;

  // Shared memory:
  // Reader reads tail, read/writes to head.
  // Writer reads head, read/writes to tail.
  volatile size_t tail; // Where new writes happen
  volatile size_t head; // Where new reads happen

  // Methods with _locked suffix assume that they are called under a lock.
  size_t used_locked();
  size_t unused_locked();
  void enqueue_locked(const char* msg, size_t size, LogFileStreamOutput* output, const LogDecorations decorations);

  // Align a c-str of size sz to the correct alignment, effectively padding it if need be.
  size_t aligned_string_size(size_t sz);

public:
  NONCOPYABLE(CircularLogBuffer);
  CircularLogBuffer(StatisticsMap& stats, PlatformMonitor& stats_lock, size_t size);

  // size shall include the NUL byte when passed in.
  void enqueue(const char* msg, size_t size, LogFileStreamOutput* output,
               const LogDecorations decorations);
  void enqueue(LogFileStreamOutput& output, LogMessageBuffer::Iterator msg_iterator);

  enum DequeueResult {
    NoMessage, // There was no message in the buffer
    TooSmall,  // The provided out buffer is too small
    OK         // A message was found and copied over to the out buffer and out_message.
  };
  DequeueResult dequeue(Message* out_message, char* out, size_t out_size);

  // Await flushing, blocks until signal_flush() is called by the flusher.
  void flush();
  void signal_flush();

  bool has_message();
  void await_message();
};

#endif // SHARE_LOGGING_CIRCULARSTRINGBUFFER_HPP
