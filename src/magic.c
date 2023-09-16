#include "magic.h"

#include <math.h>
#include <stdio.h>

unsigned n_digits(double num) {
  if (num == 0) {
    return 1;			/* Zero has one digit when written out. */
  } else {
    return ((unsigned) floor(log10(fabs(num)))) + 1;    
  }
}

void indent_by(unsigned n_spaces) {
  for (unsigned i = 0; i < n_spaces; i++) {
    printf(" ");
  }
}
