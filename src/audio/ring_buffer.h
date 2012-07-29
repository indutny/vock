#ifndef _SRC_RING_BUFFER_H_
#define _SRC_RING_BUFFER_H_

#include <stdlib.h> // NULL
#include <string.h> // memcpy
#include <sys/types.h> // size_t

class RingBuffer {
 public:
  struct Buffer {
    char* data;
    size_t offset;
    size_t size;

    Buffer* prev;
    Buffer* next;
  };

  RingBuffer(size_t size) : default_size_(size) {
    head_ = tail_ = CreateBuffer(size);
    head_->prev = head_->next = head_;
    size_ = 0;
  }

  ~RingBuffer() {
    Buffer* buffer = head_;
    do {
      delete[] buffer->data;
      delete buffer;
      buffer = buffer->next;
    } while (buffer != head_);
  }

  inline Buffer* CreateBuffer(size_t size) {
    Buffer* result;

    result = new Buffer();
    result->data = new char[size];
    result->size = size;
    result->offset = 0;

    return result;
  }

  inline size_t Size() {
    return size_;
  }

  // Get buffer of fixed size from the circle
  inline char* Produce(size_t size) {
    size_ += size;

    while (tail_->offset + size > tail_->size) {
      // If next buffer is free - move tail to it and reuse it!
      if (tail_->next->offset == 0 && tail_->next != head_) {
        tail_ = tail_->next;
      } else {
        // No free space is available in circle - allocate new buffer
        Buffer* buffer;

        buffer = CreateBuffer(size > default_size_ ? size : default_size_);

        // And insert it in the circle after the tail
        buffer->prev = head_->prev;
        buffer->next = head_;
        head_->prev->next = buffer;
        head_->prev = buffer;
        tail_ = buffer;
      }
    }

    char* result = tail_->data + tail_->offset;
    tail_->offset += size;
    return result;
  }

  // Flush all data in circle size_to given buffer
  inline void Flush(char* out) {
    Buffer* buffer = head_;
    do {
      // Copy data
      memcpy(out, buffer->data, buffer->offset);
      out += buffer->offset;

      // Reset buffer
      buffer->offset = 0;

      buffer = buffer->next;
    } while (buffer != head_);

    // Reset total size
    size_ = 0;
  }

  // Fill buffer with as much data as we have
  inline size_t Fill(char* out, size_t bytes) {
    size_t written;

    // Ring buffer has less bytes than we must output
    if (size_ < bytes) {
      written = Size();
      Flush(out);
      return written;
    }

    written = bytes;
    while (bytes > 0) {
      size_t to_write = bytes > head_->offset ? head_->offset : bytes;
      memcpy(out, head_->data, to_write);

      // Shift bytes in head if there are some bytes left
      if (to_write != head_->size) {
        head_->offset -= to_write;
        memmove(head_->data, head_->data + to_write, head_->offset);
      } else {
        // Reset current head and move to the next one
        head_->offset = 0;
        head_ = head_->next;
      }

      out += to_write;
      bytes -= to_write;
    }
    size_ -= written;

    return written;
  }

 private:
  Buffer* head_;
  Buffer* tail_;
  size_t size_;
  size_t default_size_;
};

#endif // _SRC_RING_BUFFER_H_
