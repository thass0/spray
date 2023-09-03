const int a = 3; /* Global variable. */

long blah(long b, long c) {
  if (b > c) {
    return b - c;
  } else {
    return c - b;
  }
}

int main(void) {
  int a = 19;
  long x = 0;
  long b = 5;
  long c = 9;
  c = blah(b, c);
  return 0;
}
