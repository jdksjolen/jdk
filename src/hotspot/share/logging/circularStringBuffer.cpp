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

#include "precompiled.hpp"
#include "logging/circularStringBuffer.hpp"
#include "runtime/os.inline.hpp"

// LogDecorator::None applies to 'constant initialization' because of its constexpr constructor.
const LogDecorations& CircularStringBuffer::None = LogDecorations(
    LogLevel::Warning, LogTagSetMapping<LogTag::__NO_TAG>::tagset(), LogDecorators::None);

const char* allocation_failure_msg = "Failed to allocate async logging buffer";

CircularMapping::CircularMapping(size_t size)
  : buffer(nullptr),
    size(size) {
  buffer = os::reserve_memory(size, false, mtLogging);
  if (buffer == nullptr) {
    vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "%s", allocation_failure_msg);
  }
  bool ret = os::commit_memory(buffer, size, false);
  if (!ret) {
    vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "%s", allocation_failure_msg);
  }
}

CircularStringBuffer::CircularStringBuffer(StatisticsMap& map, PlatformMonitor& stats_lock,
                                           size_t size, bool stalling_enabled)
  : _stats(map),
    _stats_lock(stats_lock),
    _consumer_lock(),
    _producer_lock(),
    _flush_sem(),
    _circular_mapping(size),
    _tail(0),
    _head(0),
    _stalling_enabled(stalling_enabled),
    _stalled_message(nullptr),
    _stalling_sem()
    {}

size_t CircularStringBuffer::allocated_bytes() {
  size_t h = Atomic::load(&_head);
  size_t t = Atomic::load(&_tail);
  if (h <= t) {
    return t - h;
  } else {
    return _circular_mapping.size - (h - t);
  }
}

size_t CircularStringBuffer::available_bytes() {
  return _circular_mapping.size - allocated_bytes();
}

size_t CircularStringBuffer::calculate_bytes_needed(size_t sz) {
  return align_up(sz, alignof(Message));
}

// Size including NUL byte
void CircularStringBuffer::enqueue_locked(const char* str, size_t size, LogFileStreamOutput* output,
                                   const LogDecorations decorations) {
  const size_t required_memory = calculate_bytes_needed(size);

#ifdef ASSERT
  size_t unused = this->available_bytes();
  // We need space for an additional Message in case of a flush token
  assert(!(output == nullptr) || unused >= sizeof(Message), "invariant");
#endif

  auto not_enough_memory = [&]() {
    return this->available_bytes() < (required_memory + sizeof(Message)*(output == nullptr ? 1 : 2));
  };

  if (not_enough_memory()) {
    if (stalling_enabled()) {
      Message* ptr = (Message*)os::malloc(size + sizeof(Message), mtLogging);
      new (ptr) Message{required_memory, output, decorations};
      ::memcpy((char*)(ptr + 1), str, size);

      assert(_stalled_message == nullptr, "Should not have two stalled messages");
      Atomic::store(&_stalled_message, ptr);
      stall();
      return;
    } else {
      _stats_lock.lock();
      bool p_created;
      uint32_t* counter = _stats.put_if_absent(output, 0, &p_created);
      *counter = *counter + 1;
      _stats_lock.unlock();
      return;
    }
  }

  // Load the tail.
  size_t t = _tail;
  // Write the Message
  Message msg{required_memory, output, decorations};
  _circular_mapping.write_bytes(t, (char*)&msg, sizeof(Message));
  // Move t forward
  t = (t +  sizeof(Message)) % _circular_mapping.size;
  // Write the string
  _circular_mapping.write_bytes(t, str, size);
  // Finally move the tail, making the message available for consumers.
  Atomic::store(&_tail, (t + required_memory) % _circular_mapping.size);
  // We're done, notify a potentially awaiting consumer.
  _consumer_lock.notify();
  return;
}

void CircularStringBuffer::enqueue(const char* msg, size_t size, LogFileStreamOutput* output,
                                   const LogDecorations decorations) {
  ProducerLocker pl(this);
  enqueue_locked(msg, size, output, decorations);
}

void CircularStringBuffer::enqueue(LogFileStreamOutput& output, LogMessageBuffer::Iterator msg_iterator) {
  ProducerLocker pl(this);
  for (; !msg_iterator.is_at_end(); msg_iterator++) {
    const char* str = msg_iterator.message();
    size_t len = strlen(str);
    enqueue_locked(str, len+1, &output, msg_iterator.decorations());
  }
}

CircularStringBuffer::DequeueResult CircularStringBuffer::dequeue(Message* out_msg, char* out, size_t out_size) {
  ConsumerLocker cl(this);

  size_t h = _head;
  size_t t = _tail;
  // Check if there's something to read
  if (h == t) {
    return NoMessage;
  }

  // Read the message
  _circular_mapping.read_bytes(h, (char*)out_msg, sizeof(Message));
  const size_t str_size = out_msg->size;
  if (str_size > out_size) {
    // Not enough space
    return TooSmall;
  }
  // Move h forward
  h = (h + sizeof(Message)) % _circular_mapping.size;

  // Now read the string
  _circular_mapping.read_bytes(h, out, str_size);
  // Done, move the head forward
  Atomic::store(&_head, (h + out_msg->size) % _circular_mapping.size);
  // Notify a producer that more memory is available
  _consumer_lock.notify();
  // Release the lock
  return Ok;
}

void CircularStringBuffer::flush() {
  enqueue("", 0, nullptr, CircularStringBuffer::None);
  _consumer_lock.notify();
  _flush_sem.wait();
}

void CircularStringBuffer::signal_flush() {
  _flush_sem.signal();
}

bool CircularStringBuffer::maybe_has_message() {
  size_t h = Atomic::load(&_head);
  size_t t = Atomic::load(&_tail);
  return !(h == t);
}

void CircularStringBuffer::await_message() {
  while (true) {
    ConsumerLocker cl(this);
    while (_head == _tail) {
      _consumer_lock.wait(0 /* no timeout */);
    }
    break;
  }
}

void CircularStringBuffer::stall() {
  _stalling_sem.wait();
}

void CircularStringBuffer::stall_finished() {
  Atomic::store(&_stalled_message, (Message*)nullptr);
  _stalling_sem.signal();
}

CircularStringBuffer::Message* CircularStringBuffer::stalled_message() {
  return const_cast<Message*>(Atomic::load(&_stalled_message));
}

char* CircularStringBuffer::stalled_string() {
  assert(_stalled_message != nullptr, "must exist");
  return (char*)(_stalled_message + 1);
}
