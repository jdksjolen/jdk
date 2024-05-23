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
#include "utilities/globalDefinitions.hpp"
#include "utilities/resourceHash.hpp"

#include <string.h>
#ifdef LINUX
#include <sys/mman.h>
#endif

// The CircularMapping is a struct that provides
// an interface for writing and reading bytes in a circular buffer
// correctly.
struct CircularMapping {
  char* buffer;
  size_t size;
  CircularMapping()
  : buffer(nullptr), size(0) {
  };
  CircularMapping(size_t size);

  void write_bytes(size_t at, const char* bytes, size_t size) {
    const size_t part1_size = MIN2(size, this->size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(&buffer[at], bytes, part1_size);
    ::memcpy(buffer, &bytes[part1_size], part2_size);
  }

  void read_bytes(size_t at, char* out, size_t size) {
    const size_t part1_size = MIN2(size, this->size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(out, &buffer[at], part1_size);
    ::memcpy(&out[part1_size], buffer, part2_size);
  }

  ~CircularMapping() {
    os::release_memory(buffer, size);
  }
};

class CircularStringBuffer {
  friend class AsyncLogTest;

public:
    // account for dropped messages
  using StatisticsMap = ResourceHashtable<LogFileStreamOutput*, uint32_t, 17, /*table_size*/
                                        AnyObj::C_HEAP, mtLogging>;
private:
  static const LogDecorations& None;
  const bool _should_stall; // Should a producer stall until a consumer has made room for its message?

  // Need to perform accounting of statistics under a separate lock.
  StatisticsMap& _stats;
  PlatformMonitor& _stats_lock;

  // Can't use a Monitor here as we need a low-level API that can be used without Thread::current().
  // The consumer lock's condition variable is used for communicating when messages are produced and consumed.
  PlatformMonitor _consumer_lock;
  PlatformMonitor _producer_lock;
  Semaphore _flush_sem;

  struct ConsumerLocker : public StackObj {
    CircularStringBuffer* buf;
    ConsumerLocker(CircularStringBuffer* buf) : buf(buf) {
      buf->_consumer_lock.lock();
    }
    ~ConsumerLocker() {
      buf->_consumer_lock.unlock();
    }
  };
  struct ProducerLocker : public StackObj {
    CircularStringBuffer* buf;
    ProducerLocker(CircularStringBuffer* buf) : buf(buf) {
      buf->_producer_lock.lock();
    }
    ~ProducerLocker() {
      buf->_producer_lock.unlock();
    }
  };
  // Opaque circular mapping of our buffer.
  CircularMapping circular_mapping;

  // Shared memory:
  // Consumer reads tail, writes to head.
  // Producer reads head, writes to tail.
  volatile size_t _tail; // Where new writes happen
  volatile size_t _head; // Where new reads happen

  size_t allocated_bytes();
  size_t available_bytes();
  // How many bytes are needed to store a message of size sz?
  size_t calculate_bytes_needed(size_t sz);

public:
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
  void enqueue_locked(const char* msg, size_t size, LogFileStreamOutput* output, const LogDecorations decorations);

public:
  NONCOPYABLE(CircularStringBuffer);
  CircularStringBuffer(StatisticsMap& stats, PlatformMonitor& stats_lock, size_t size, bool should_stall = false);

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

  bool maybe_has_message();
  void await_message();
};

#endif // SHARE_LOGGING_CIRCULARSTRINGBUFFER_HPP
