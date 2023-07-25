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
  int product = mul(9, 3);
  int sum = add(product, 6);
  printf("Product: %d; Sum: %d\n", product, sum);
  return 0;
}

