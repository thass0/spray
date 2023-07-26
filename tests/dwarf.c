#include <libdwarf-0/libdwarf.h>

#include "test_utils.h"

#define UNIT_TESTS
#include "../src/info.h"
#include "../src/spray_dwarf.h"

#include <limits.h>
#include <stdlib.h>

enum {
  RAND_DATA_BUF_SIZE = 32,
};

TEST(get_line_entry_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  {  /* Happy path. */
    x86_addr pc = { 0x00401156 };
    LineEntry line_entry = sd_line_entry_from_pc(dbg, pc);
    assert_true(line_entry.is_ok);
    assert_int(line_entry.ln, ==, 11);
    assert_int(line_entry.cl, ==, 7);
    assert_ptr_not_null(line_entry.filepath);
    /* Ignore the part of the filepath that is host specific. */
    assert_ptr_not_null(strstr(line_entry.filepath, SIMPLE_SRC));
  }
  {  /* Sad path 😢. */
    x86_addr pc = { 0xdeabbeef };
    LineEntry line_entry = sd_line_entry_from_pc(dbg, pc);
    assert_false(line_entry.is_ok);
    assert_ptr_equal(line_entry.filepath, NULL);
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
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  unsigned lines[5];

  char *filepath = realpath(SIMPLE_SRC, NULL);
  sd_for_each_line_in_subprog(dbg, "main", filepath, callback__store_line,
                              &lines);
  dwarf_finish(dbg);
  free(filepath);

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
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);
  int res = DW_DLV_OK;

  res = sd_search_dwarf_dbg(dbg,
                            &error,
                            callback__test_search,
                            "this_function_name_does_not_exist",
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

SprayResult test_get_effective_start_addr(Dwarf_Debug dbg,
                                          const DebugSymbol *sym,
                                          x86_addr *dest) {
  return sd_effective_start_addr(dbg, sym_start_addr(sym), sym_end_addr(sym),
                                 dest);
}

TEST(get_effective_function_start_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);
  DebugInfo *info = init_debug_info(SIMPLE_64BIT_BIN);
  assert_ptr_not_null(info);
  const DebugSymbol *sym = sym_by_name("main", info);
  assert_ptr_not_null(sym);

  x86_addr main_start = { 0 };
  SprayResult res = test_get_effective_start_addr(dbg, sym, &main_start);
  assert_int(res, ==, SP_OK);
  LineEntry line_entry = sd_line_entry_from_pc(dbg, main_start);
  assert_true(line_entry.is_ok);
  /* 10 is the line number of the first line after the function declaration. */
  assert_int(line_entry.ln, ==, 10);
  
  /* `weird_sum` has a multi-line function declaration. */
  sym = sym_by_name("weird_sum", info);
  x86_addr func_start = { 0 };
  res = test_get_effective_start_addr(dbg, sym, &func_start);
  assert_int(res, ==, SP_OK);
  line_entry = sd_line_entry_from_pc(dbg, func_start);
  assert_true(line_entry.is_ok);
  /* 10 is the line number of the first line after the function declaration. */
  assert_int(line_entry.ln, ==, 3);
  
  dwarf_finish(dbg);
  free_debug_info(&info);

  return MUNIT_OK;
}

TEST(get_filepath_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  {
    x86_addr pc = { 0x00401156 };
    char *filepath = sd_filepath_from_pc(dbg, pc);
    assert_ptr_not_null(filepath);
    char *expect_filepath = realpath(SIMPLE_SRC, NULL);
    assert_string_equal(filepath, expect_filepath);
    free(filepath);
    free(expect_filepath);
  }
  {  /* Sad path. */
    x86_addr pc = { 0xdeadbeef };
    char *no_filepath = sd_filepath_from_pc(dbg, pc);
    assert_ptr_equal(no_filepath, NULL);
  }

  dwarf_finish(dbg);
  return MUNIT_OK;
}

TEST(sd_line_entry_at_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  LineEntry line = sd_line_entry_at(dbg, SIMPLE_SRC, 4);
  assert_true(line.is_ok);
  assert_int(line.ln, ==, 4);

  dwarf_finish(dbg);

  return MUNIT_OK;
}

MunitTest dwarf_tests[] = {
    REG_TEST(get_line_entry_from_pc_works),
    REG_TEST(iterating_lines_works),
    REG_TEST(search_returns_the_correct_result),
    REG_TEST(get_effective_function_start_works),
    REG_TEST(get_filepath_from_pc_works),
    REG_TEST(sd_line_entry_at_works),
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};
