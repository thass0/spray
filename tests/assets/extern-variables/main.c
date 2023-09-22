#include <stdio.h>

/*
 The files in this directory were created to test
 that the file path to the file where some variable
 was declared is found correctly.
*/

extern int blah_int1;		/* Declared in first_file.c */
extern int blah_int_another;	/* Declared in third_file.c */
extern int blah_int2;		/* Declared in second_file.c */
int my_own_int = 8;

int main(void) {
  int sum = blah_int1 + my_own_int + blah_int2 + blah_int_another;
  printf("sum: %d\n", sum);
  return 0;
}
