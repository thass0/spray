int weird_sum(int a, int b) {
  int c = a + 1;
  int d = b + 2;
  int e = c + d;
  return e;
}

int main(void) {
  int a = 7;
  int b = 11;
  int c = weird_sum(a, b);
  return 0;
}
