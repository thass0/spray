#include <stdio.h>

int main(int argc, char *argv[]) {
  printf("Command line arguments: ");

  for (int i = 0; i < argc; i++) {
    printf("%s", argv[i]);
    if (i + 1 < argc) {
      printf(" ");
    } else  {
      printf("\n");
    }
  }

  return 0;
}
