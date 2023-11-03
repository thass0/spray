#include "header.h"

/* The executable compiled from this file and `header.h`
 * contains a single CU with multiple files (`main.c` and
 * `header.h`) in the line table header. It's used to test
 * retrieving the place were a variable was declared. */

int here = 9;

int main(void) {
  int sum = here + blah;
  return sum;
}
