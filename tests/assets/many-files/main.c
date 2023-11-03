#include "foo1.h"
#include "foo2.h"
#include "bar1.h"
#include "bar2.h"

/* A binary comprised of a number of files
 * used to test the names of all files used
 * in a binary.
 * The main.c CU's line header table should
 * contain the file names bar1.h, bar2.h and
 * main.c. The line header tables for foo1.c
 * should contain only foo
 */

int
main (void)
{
  return bar1 + bar2 + foo1 () + foo2 ();
}
  
