int a = 1;
const long b = 2;
void *c = (void *) 3;
long long *d = (long long *) 4;
const unsigned *e = (const unsigned *) 5;
int *const f = (int *const) 6;
volatile const char *restrict const g = (const char *const)7;


int main(void) {
  char h = 'a';
  unsigned char i = 'b';
  signed char j = 'c';
  const char k = 'd';
  const unsigned char l = 'e';
  const signed char m = 'f';
  unsigned long long n = (unsigned long long) 1 << 63;

  return 0;
}
