// [µnit test framework](https://github.com/nemequ/munit)
#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

extern MunitTest parse_elf_tests[];
extern MunitTest dwarf_tests[];

static MunitSuite suites[] = {
  {
    "/parse_elf",
    parse_elf_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
  },
  {
    "/dwarf_tests",
    dwarf_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
  },
  { NULL, NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE }
};

static const MunitSuite suite = {
  "/spray",
  NULL,
  suites,
  1,
  MUNIT_SUITE_OPTION_NONE,
};

int main(int argc, char* const* argv) {
  return munit_suite_main(&suite, NULL, argc, argv);
}
