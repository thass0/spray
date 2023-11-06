int main(void) {
  int i = 42;
  int *ip = &i;
  /* Some value likely to dereference to something: */
  long ptr = (long) ip;
  char *x = "This is a test";
  return x[0];
}
