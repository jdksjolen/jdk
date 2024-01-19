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
const LogDecorations& CircularLogBuffer::None = LogDecorations(
    LogLevel::Warning, LogTagSetMapping<LogTag::__NO_TAG>::tagset(), LogDecorators::None);

CircularLogBuffer::CircularLogBuffer(StatisticsMap& map, PlatformMonitor& stats_lock, size_t size)
  : _stats(map),
    _stats_lock(stats_lock),
    _buffer(size),
    tail(0),
    head(0) {}

size_t CircularLogBuffer::used_locked() {
  size_t h = head;
  size_t t = tail;
  if (h <= t) {
    return t - h;
  } else {
    return _buffer.size - (h - t);
  }
}
size_t CircularLogBuffer::unused_locked() {
  return _buffer.size - used_locked();
}

size_t CircularLogBuffer::aligned_string_size(size_t sz) {
  return align_up(sz, alignof(Message));
}

void CircularLogBuffer::enqueue_locked(const char* str, size_t size, LogFileStreamOutput* output,
                                   const LogDecorations decorations) {
  const size_t required_str_memory = aligned_string_size(size);
  const size_t unused = unused_locked();

  // We always leave space for a flush token when enqueueing a regular message.
  assert(!(output == nullptr) || unused >= sizeof(Message), "must have space for flush token");
  if (unused < (required_str_memory + sizeof(Message)*(output == nullptr ? 1 : 2))) {
    StatsLocker sl(this);
    bool p_created;
    uint32_t* counter = _stats.put_if_absent(output, 0, &p_created);
    *counter = *counter + 1;
    return;
  }

  size_t t = tail;
  // Write the Message
  Message msg(required_str_memory, output, decorations);
  _buffer.write_bytes(t, (char*)&msg, sizeof(Message));
  // Write the string
  _buffer.write_bytes(t + sizeof(Message), str, size);
  // Finally move the tail, making the message available for consumers.
  tail = (t + required_str_memory + sizeof(Message)) % _buffer.size;
  // We're done, notify the reader.
  _read_lock.notify();
  return;
}

void CircularLogBuffer::enqueue(const char* msg, size_t size, LogFileStreamOutput* output,
                                   const LogDecorations decorations) {
  assert(msg != nullptr, "use \"\" and size 0 for no string");
  WriteLocker rl(this);
  enqueue_locked(msg, size, output, decorations);
}

void CircularLogBuffer::enqueue(LogFileStreamOutput& output, LogMessageBuffer::Iterator msg_iterator) {
  WriteLocker wl(this);
  for (; !msg_iterator.is_at_end(); msg_iterator++) {
    const char* str = msg_iterator.message();
    size_t len = strlen(str);
    enqueue_locked(str, len+1, &output, msg_iterator.decorations());
  }
}

CircularLogBuffer::DequeueResult CircularLogBuffer::dequeue(Message* out_msg, char* out, size_t out_size) {
  ReadLocker rl(this);

  size_t h = head;
  size_t t = tail;

  if (h == t) {
    return NoMessage;
  }

  // Read the message
  _buffer.read_bytes(h, (char*)out_msg, sizeof(Message));
  const size_t str_size = out_msg->size;
  if (str_size > out_size) {
    // Not enough space
    return TooSmall;
  }

  // Read the string
  _buffer.read_bytes(h + sizeof(Message), out, str_size);
  head = (h + out_msg->size + sizeof(Message)) % _buffer.size;
  return OK;
}

void CircularLogBuffer::flush() {
  enqueue("", 0, nullptr, CircularLogBuffer::None);
  _read_lock.notify();
  _flush_sem.wait();
}

void CircularLogBuffer::signal_flush() {
  _flush_sem.signal();
}

bool CircularLogBuffer::has_message() {
  size_t h = Atomic::load(&head);
  size_t t = Atomic::load(&tail);
  return !(h == t);
}

void CircularLogBuffer::await_message() {
  while (true) {
    ReadLocker rl(this);
    while (!has_message()) {
      _read_lock.wait(0 /* no timeout */);
    }
    break;
  }
}
