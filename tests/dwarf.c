#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/spray_dwarf.h"

#include <stdio.h>

TEST(get_function_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init("tests/assets/linux_x86_bin", &error);
  assert_ptr_not_null(dbg);

  /* The PC value was acquired using `dwarfdump`. It lies
     inside the PC range of the `main` function. */
  x86_addr pc = { 0x00401122 };
  char *fn_name = get_function_from_pc(dbg, pc);

  assert_ptr_not_null(fn_name);
  assert_string_equal(fn_name, "main");
  free(fn_name);
  dwarf_finish(dbg);
  return MUNIT_OK;
}

TEST(get_line_entry_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init("tests/assets/linux_x86_bin", &error);
  assert_ptr_not_null(dbg);

  {  /* Happy path. */
    x86_addr pc = { 0x0040112c };
    LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
    assert_int(line_entry.ln, ==, 4);
    assert_int(line_entry.cl, ==, 13);
  }
  {  /* Unhappy path. */
    x86_addr pc = { 0xdeabbeef };
    LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
    /* -1 indicates error. */
    assert_int(line_entry.ln, ==, -1);
    assert_int(line_entry.cl, ==, -1);
  }

  dwarf_finish(dbg);
  return MUNIT_OK;
}

MunitTest dwarf_tests[] = {
  REG_TEST(get_function_from_pc_works),  
  REG_TEST(get_line_entry_from_pc_works),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};

