#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/spray_dwarf.h"

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

MunitTest dwarf_tests[] = {
  REG_TEST(get_function_from_pc_works),  
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};

