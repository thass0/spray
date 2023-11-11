#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void print_concat(char *a, char *b) {
  char *buf;
  int a_len = strlen(a);
  int b_len = strlen(b);

  if (b_len > 0) {
    buf = malloc(a_len + b_len + 1);
    strcpy(buf, a);
    strcpy(buf + a_len, b);
    puts(buf);
  } else {
    puts(a);
  }

  free(buf);
}

int main(void) {
  print_concat("foo", "bar");
  print_concat("foo", "");
  return 0;
}
