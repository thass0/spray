#include <stdio.h>

struct Rational {
  int numer;
  int denom;  
};

void print_rat(struct Rational rat) {
  printf("%d / %d\n", rat.numer, rat.denom);
}

int main(void) {
  struct Rational rat = (struct Rational) { 5, 3 };
  rat.numer = 9;
  printf("The numerator is: %d\n", rat.numer);
  print_rat(rat);
  return 0;
}

