#include "file2.h"

int file2_compute_something(int n) {
  if (n < 2) {
    return n;
  } else {
    return   file2_compute_something(n - 1)
           + file2_compute_something(n - 2);
  }
}
