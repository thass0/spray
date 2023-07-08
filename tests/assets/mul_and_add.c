#include <stdio.h>

int add(int a, int b) {
  int c = a + b;
  return c;
}

int mul(int a, int b) {
  int acc = 0;
  for (int i = 0; i < b; i++) {
    acc = add(acc, a);
  }
  return acc;
}

int main(void) {
  int sum = add(5, 6);
  int product = mul(sum, 3);
  printf("Sum: %d; Product: %d\n", sum, product);
  return 0;
}

