#ifndef _SRC_CIRCLE_H_
#define _SRC_CIRCLE_H_

#include <stdlib.h>

class Circle {
 public:
  struct Buffer {
    char* data;
    char* pos;
    int size;

    Buffer* prev;
    Buffer* next;
  };

  Circle(int size) {
  }
  ~Circle() {
  }

  inline void Produce(char* data, int size) {
  }

  inline char* Consume(int size) {
    return NULL;
  }

  inline void Release(char* data, int size) {
  }

 private:
  Buffer* head_;
};

#endif // _SRC_CIRCLE_H_
