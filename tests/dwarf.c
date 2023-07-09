#include "libdwarf.h"
#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#define UNIT_TESTS
#include "../src/spray_dwarf.h"

#include <limits.h>

#define BIN_NAME "tests/assets/linux_x86_bin"
#define SRC_NAME "tests/assets/debug_me.c"

enum {
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
    assert_int(line_entry.ln, ==, 11);
    assert_int(line_entry.cl, ==, 7);
    assert_ptr_not_null(line_entry.filepath);
    /* Ignore the part of the filepath that is host specific. */
    assert_ptr_not_null(strstr(line_entry.filepath, SRC_NAME));
  }
  {  /* Sad path 😢. */
    x86_addr pc = { 0xdeabbeef };
    LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
    assert_false(line_entry.is_ok);
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

SprayResult callback__store_line(LineEntry *line, void *const void_data) {
  assert(line != NULL);
  assert(void_data != NULL);

  static int i = 0;
  unsigned *lines = (unsigned *) void_data;
  assert(i < 5);
  lines[i++] = line->ln;

  return SP_OK;
}

TEST(iterating_lines_works)  {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);

  unsigned lines[5];
  for_each_line_in_subprog(dbg,
                           "main",
                           callback__store_line,
                           &lines);
  dwarf_finish(dbg);

  unsigned expect[5] = {9, 10, 11, 12, 13};
  assert_memory_equal(sizeof(unsigned[5]), lines, expect);
  
  return MUNIT_OK;
}

bool callback__test_search(Dwarf_Debug dbg,
                           Dwarf_Die die,
                           const void *const search_for,
                           void *const search_findings
) {  
  unused(search_findings);
  assert(dbg != NULL);
  assert(die != NULL);

  const char *const fn_name = (char *) search_for;
  if (sd_is_subprog_with_name(dbg, die, fn_name)) {
    return true;
  } else {
    return false;
  }
}

TEST(search_returns_the_correct_result) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);
  int res = DW_DLV_OK;

  res = sd_search_dwarf_dbg(dbg,
                            &error,
                            callback__test_search,
                            "this_function_name_doesnt_exist",
                            NULL);
  assert_int(res, ==, DW_DLV_NO_ENTRY);

  res = sd_search_dwarf_dbg(dbg,
                            &error,
                            callback__test_search,
                            "main",  // <- This does exist.
                            NULL);
  assert_int(res, ==, DW_DLV_OK);

  dwarf_finish(dbg);

  return MUNIT_OK;
}

TEST(get_function_start_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = dwarf_init(BIN_NAME, &error);
  assert_ptr_not_null(dbg);
  
  x86_addr main_start = { 0 };
  SprayResult res = get_function_start_addr(dbg, "main", &main_start);
  assert_int(res, ==, SP_OK);
  LineEntry line_entry = get_line_entry_from_pc(dbg, main_start);
  assert_true(line_entry.is_ok);
  /* 10 is the line number of the first line after the function declaration. */
  assert_int(line_entry.ln, ==, 10);
  
  /* `weird_sum` has a multi-line function declaration. */
  x86_addr func_start = { 0 };
  res = get_function_start_addr(dbg, "weird_sum", &func_start);
  assert_int(res, ==, SP_OK);
  line_entry = get_line_entry_from_pc(dbg, func_start);
  assert_true(line_entry.is_ok);
  /* 10 is the line number of the first line after the function declaration. */
  assert_int(line_entry.ln, ==, 3);
  
  dwarf_finish(dbg);
  
  return MUNIT_OK;
}

MunitTest dwarf_tests[] = {
  REG_TEST(get_function_from_pc_works),  
  REG_TEST(get_line_entry_from_pc_works),
  REG_TEST(get_low_and_high_pc_works),
  REG_TEST(iterating_lines_works),
  REG_TEST(search_returns_the_correct_result),
  REG_TEST(get_function_start_works),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};

