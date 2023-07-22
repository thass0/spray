/* Test utilities. */

#define MUNIT_ENABLE_ASSERT_ALIASES
#include "../dependencies/munit/munit.h"

// Create a test
#define MUNIT_TEST(name) \
  static MunitResult name(MUNIT_UNUSED const MunitParameter p[], MUNIT_UNUSED void* fixture)

// Register a test.
#define MUNIT_REG_TEST(name) \
  { "/"#name, name, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL } 
