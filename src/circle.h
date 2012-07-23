#ifndef _SRC_CIRCLE_H_
#define _SRC_CIRCLE_H_

#include <stdlib.h> // NULL
#include <string.h> // memcpy
#include <sys/types.h> // size_t

class Circle {
 public:
  struct Buffer {
    char* start;
    char* current;
    char* end;
    size_t size;

    Buffer* prev;
    Buffer* next;
  };

  Circle(size_t size) : default_size_(size) {
    head_ = tail_ = CreateBuffer(size);
    head_->prev = head_->next = head_;
    size_ = 0;
  }

  ~Circle() {
    Buffer* buffer = head_;
    do {
      delete buffer->start;
      delete buffer;
      buffer = buffer->next;
    } while (buffer != head_);
  }

  inline Buffer* CreateBuffer(size_t size) {
    Buffer* result;

    result = new Buffer();
    result->start = result->current = new char[size];
    result->end = result->start + size;
    result->size = 0;

    return result;
  }

  inline size_t Size() {
    return size_;
  }

  // Get buffer of fixed size from the circle
  inline char* Produce(size_t size) {
    size_ += size;

    if (tail_->current + size > tail_->end) {
      // If next buffer is free - move tail to it and reuse it!
      if (tail_->next->size == 0 && tail_->next != head_) {
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

    char* result = tail_->current;
    tail_->current += size;
    tail_->size += size;
    return result;
  }

  // Flush all data in circle size_to given buffer
  inline void Flush(char* out) {
    Buffer* buffer = head_;
    do {
      // Copy data
      memcpy(out, buffer->start, buffer->size);
      out += buffer->size;

      // Reset buffer
      buffer->current = buffer->start;
      buffer->size = 0;

      buffer = buffer->next;
    } while (buffer != head_);
    size_ = 0;
  }

  // Fill buffer with as much data as we have
  inline size_t Fill(char* out, size_t bytes) {
    size_t written;

    // Circle has less bytes than we must output
    if (size_ < bytes) {
      written = Size();
      Flush(out);
      return written;
    }

    written = bytes;
    while (bytes > 0) {
      size_t to_write = bytes > head_->size ? head_->size : bytes;
      memcpy(out, head_->start, to_write);

      // Shift bytes in head if there are some bytes left
      if (to_write != head_->size) {
        head_->size -= to_write;
        memmove(head_->start, head_->start + to_write, head_->size);
      } else {
        // Reset current head and move to the next one
        head_->current = head_->start;
        head_->size = 0;
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

#endif // _SRC_CIRCLE_H_
