#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/spray_dwarf.h"

#include <limits.h>

#define BIN_NAME "tests/assets/linux_x86_bin"
#define SRC_NAME "tests/assets/debug_me.c"

enum magic {
  RAND_DATA_BUF_SIZE = 32,
};

TEST(get_function_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);

  {  /* Happy path 😚. */
    /* The PC value was acquired using `dwarfdump`. It lies
       inside the PC range of the `main` function. */
    x86_addr pc = { 0x00401156 };
    char *fn_name = get_function_from_pc(dbg, pc);
    assert_ptr_not_null(fn_name);
    assert_string_equal(fn_name, "main");
    free(fn_name);
  }
  {  /* Sad path. */
    x86_addr pc = { 0xdeadbeef };
    char *no_fn_name = get_function_from_pc(dbg, pc);
    assert_ptr_equal(no_fn_name, NULL);
  }

  dwarf_finish(dbg);
  return MUNIT_OK;
}

TEST(get_line_entry_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);

  {  /* Happy path. */
    x86_addr pc = { 0x00401156 };
    LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
    assert_int(line_entry.ln, ==, 10);
    assert_int(line_entry.cl, ==, 7);
    assert_ptr_not_null(line_entry.filepath);
    /* Ignore the part of the filepath that is host specific. */
    assert_ptr_not_null(strstr(line_entry.filepath, SRC_NAME));
  }
  {  /* Sad path 😢. */
    x86_addr pc = { 0xdeabbeef };
    LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
    /* -1 indicates error. */
    assert_int(line_entry.ln, ==, -1);
    assert_int(line_entry.cl, ==, -1);
    assert_ptr_equal(line_entry.filepath, NULL);
  }

  dwarf_finish(dbg);
  return MUNIT_OK;
}

TEST(get_low_and_high_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);

  {  /* Happy */
    /* PC retrieved using `dwarfdump`. */
    char *func = get_function_from_pc(dbg, (x86_addr) { 0x00401120 });
    x86_addr func_entry = { 0 };
    assert_true(get_at_low_pc(dbg, func, &func_entry));
    x86_addr func_end = { 0 };
    assert_true(get_at_high_pc(dbg, func, &func_end));
    free(func);
  } {  /* Sad */
    x86_addr func_entry = { 0 };
    char random_fn_name[RAND_DATA_BUF_SIZE];
    munit_rand_memory(RAND_DATA_BUF_SIZE, (uint8_t *) random_fn_name);
    assert_false(get_at_low_pc(dbg, random_fn_name, &func_entry));
    x86_addr func_end = { 0 };
    assert_false(get_at_high_pc(dbg, random_fn_name, &func_end));
  }

  dwarf_finish(dbg);

  return MUNIT_OK;
}

MunitTest dwarf_tests[] = {
  REG_TEST(get_function_from_pc_works),  
  REG_TEST(get_line_entry_from_pc_works),
  REG_TEST(get_low_and_high_pc_works),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};

