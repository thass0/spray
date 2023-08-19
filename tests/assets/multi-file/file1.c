#include "file2.h"

int file1_compute_something(int n) {
  int i = 0;
  int acc = 0;
  while (i < n) {
    acc += i * i;
    i ++;
  }
  return acc;
}

int main(void) {
  int num1 = file1_compute_something(3);
  int num2 = file2_compute_something(num1);
  (void) (num1 + num2);
  struct Blah blah = file2_init_blah(4);
  (void) blah;
  return 0;
}
