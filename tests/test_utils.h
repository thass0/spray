/* Test utilities. */

#ifndef _SPRAY_TEST_UTILS_H_
#define _SPRAY_TEST_UTILS_H_

#define MUNIT_ENABLE_ASSERT_ALIASES
#include "../dependencies/munit/munit.h"

// Names of tests assets
#define SIMPLE_SRC "tests/assets/simple.c"
#define SIMPLE_64BIT_BIN "tests/assets/64bit-linux-simple.bin"
#define SIMPLE_32BIT_BIN "tests/assets/32bit-linux-simple.bin"
#define NESTED_FUNCTIONS_SRC "tests/assets/nested_functions.c"
#define NESTED_FUNCTIONS_BIN "tests/assets/nested-functions.bin"
#define MULTI_FILE_BIN "tests/assets/multi-file.bin"
#define EXTERN_VARIABLES_BIN "tests/assets/extern-variables.bin"
#define PRINT_ARGS_SRC "tests/assets/print_args.c"
#define PRINT_ARGS_BIN "tests/assets/print-args.bin"
#define RECURRING_VARIABLES_SRC "tests/assets/recurring_variables.c"
#define RECURRING_VARIABLES_BIN "tests/assets/recurring-variables.bin"
#define POINTERS_SRC "tests/assets/pointers.c"
#define POINTERS_BIN "tests/assets/pointers.bin"
#define INCLUDE_VARIABLE_BIN "tests/assets/include-variable.bin"
#define WRONG_COMPILER_BIN "tests/assets/wrong-compiler.bin"
#define TYPE_EXAMPLES_BIN "tests/assets/type-examples.bin"

// Create a test
#define TEST(name) \
  static MunitResult name(MUNIT_UNUSED const MunitParameter p[], MUNIT_UNUSED void* fixture)

// Register a test.
#define REG_TEST(name) \
  { "/"#name, name, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL } 

#endif  // _SPRAY_TEST_UTILS_H_
