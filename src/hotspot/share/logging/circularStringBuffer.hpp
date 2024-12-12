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
#include "mutex_posix.hpp"
#include "nmt/memTag.hpp"
#include "runtime/mutex.hpp"
#include "runtime/os.inline.hpp"
#include "runtime/semaphore.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/resourceHash.hpp"

#include <string.h>

// Provide an interface for writing and reading memory as-if contiguous
// in a circular buffer.
class CircularMapping {
  char* _buffer;
  const size_t _size;

public:
  CircularMapping()
  : _buffer(nullptr), _size(0) {
  };
  CircularMapping(size_t size);

  void write_bytes(size_t at, const char* bytes, size_t size) {
    const size_t part1_size = MIN2(size, this->_size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(&_buffer[at], bytes, part1_size);
    ::memcpy(_buffer, &bytes[part1_size], part2_size);
  }

  void read_bytes(size_t at, char* out, size_t size) {
    const size_t part1_size = MIN2(size, this->_size - at);
    const size_t part2_size = size - part1_size;

    ::memcpy(out, &_buffer[at], part1_size);
    ::memcpy(&out[part1_size], _buffer, part2_size);
  }

  size_t size() { return _size; }

  ~CircularMapping() {
    os::release_memory(_buffer, _size);
  }
};

class CircularStringBuffer {
  friend class AsyncLogTest;

public:
  // Account for dropped messages.
  using StatisticsMap = ResourceHashtable<LogFileStreamOutput*, uint32_t, 17, /*table_size*/
                                        AnyObj::C_HEAP, mtLogging>;

  // Messsage is the header of a log line and contains its associated string length, decorations, and output.
  // It is directly followed by the c-str of the log line. The log line is padded at the end
  // to ensure correct alignment for the Message. A Message is considered to be a flush token
  // when its output is null.
  //
  // Example layout:
  // ---------------------------------------------
  // |size|output|decorations|"a log line", |pad| <- Message aligned.
  // |size|output|decorations|"yet another",|pad|
  // ...
  // |0|nullptr|decorations|"",|pad| <- flush token
  // ---------------------------------------------
  struct Message {
    size_t size; // Size of string following the Message envelope
    LogFileStreamOutput* const output;
    const LogDecorations decorations;
    Message(size_t size, LogFileStreamOutput* output, const LogDecorations decorations)
      : size(size),
        output(output),
        decorations(decorations) {
    }

    Message()
      : size(0),
        output(nullptr),
        decorations(None) {
    }

    bool is_token() {
      return output == nullptr;
    }
  };

private:
  static const LogDecorations& None;

  // Need to perform accounting of statistics under a separate lock.
  StatisticsMap& _stats;
  PlatformMonitor& _stats_lock;

  template<PlatformMonitor*(*Accessor)(CircularStringBuffer*)>
  struct Locker : public StackObj {
    CircularStringBuffer* buf;
    Locker(CircularStringBuffer* buf) : buf(buf) {
      Accessor(buf)->lock();
    }
    ~Locker() {
      Accessor(buf)->unlock();
    }
  };
  static PlatformMonitor* stats_lock_getter(CircularStringBuffer* cb) { return &cb->_stats_lock; }
  using StatsLocker = Locker<stats_lock_getter>;

  // Can't use a Monitor here as we need a low-level API that can be used without Thread::current().
  // The consumer lock's condition variable is used for communicating when messages are produced and consumed.
  PlatformMonitor _consumer_lock;
  PlatformMonitor _producer_lock;
  Semaphore _flush_sem;

  static PlatformMonitor* consumer_lock_getter(CircularStringBuffer* cb) { return &cb->_consumer_lock; }
  using ConsumerLocker = Locker<consumer_lock_getter>;
  static PlatformMonitor* producer_lock_getter(CircularStringBuffer* cb) { return &cb->_producer_lock; }
  using ProducerLocker = Locker<producer_lock_getter>;

  // Opaque circular mapping of our buffer.
  CircularMapping _circular_mapping;

  // Shared memory:
  // Consumer reads tail, writes to head.
  // Producer reads head, writes to tail.
  volatile size_t _tail; // Where new writes happen
  volatile size_t _head; // Where new reads happen

  // Stalling mechanism:
  bool _stalling_enabled; // Is stalling allowed?
  volatile Message* _stalled_message; // Message, followed by string, that is stalled
  PlatformMonitor _stalling_lock; // Waiting/signalling mechanism for stalled thread.

  static PlatformMonitor* stalling_lock_getter(CircularStringBuffer* cb) { return &cb->_stalling_lock; }

  size_t allocated_bytes();
  size_t available_bytes();
  // How many bytes are needed to store a message of size sz?
  size_t calculate_bytes_needed(size_t sz);

  void enqueue_locked(const char* msg, size_t size, LogFileStreamOutput* output, const LogDecorations decorations);

public:
  NONCOPYABLE(CircularStringBuffer);
  CircularStringBuffer(StatisticsMap& stats, PlatformMonitor& stats_lock, size_t size, bool stalling_enabled);

  void enqueue(const char* msg, size_t size, LogFileStreamOutput* output,
               const LogDecorations decorations);
  void enqueue(LogFileStreamOutput& output, LogMessageBuffer::Iterator msg_iterator);

  using StallingLocker = Locker<stalling_lock_getter>;

  enum DequeueResult {
    NoMessage, // There was no message in the buffer
    TooSmall,  // The provided out buffer is too small
    Ok         // A message was found and copied over to the out buffer and out_message.
  };
  DequeueResult dequeue(Message* out_message, char* out, size_t out_size);

  // Flushing interface, blocks until signal_flush() is called by the flusher.
  void flush();
  void signal_flush();

  // Stalling interface.
  bool stalling_enabled() { return _stalling_enabled; }
  void stall();
  void stall_finished();
  Message* stalled_message();
  char* stalled_string();

  bool maybe_has_message();
  void await_message();
};

#endif // SHARE_LOGGING_CIRCULARSTRINGBUFFER_HPP
