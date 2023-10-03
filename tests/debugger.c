#include "test_utils.h"

#include "../src/breakpoints.h"
#define UNIT_TESTS
#include "../src/debugger.h"

TEST(breakpoints_work) {
  Debugger dbg;
  char *prog_argv[] = {SIMPLE_64BIT_BIN, NULL};
  assert_int(setup_debugger(prog_argv[0], prog_argv, &dbg), ==, 0);

  real_addr bp_addr1 = {0x00401122};

  enable_breakpoint(dbg.breakpoints, bp_addr1);
  assert_true(lookup_breakpoint(dbg.breakpoints, bp_addr1));

  disable_breakpoint(dbg.breakpoints, bp_addr1);
  assert_false(lookup_breakpoint(dbg.breakpoints, bp_addr1));

  del_debugger(dbg);

  return MUNIT_OK;
}

#define TEST_VARLOC(test_name, bin_name, var_name, pc_value, expect)	\
  TEST ((test_name)) {							\
    Debugger dbg;							\
    char *prog_argv[] = {(bin_name), NULL};				\
    assert_int(setup_debugger(prog_argv[0], prog_argv, &dbg), ==, 0);	\
									\
    dbg_addr pc = {(pc_value)};						\
									\
    enable_breakpoint(dbg.breakpoints, dbg_to_real(dbg.load_address, pc)); \
    ExecResult exec_res = continue_execution(&dbg);			\
    assert_int(exec_res.type, ==, SP_OK);				\
    ExecResult wait_res = wait_for_signal(&dbg);			\
    assert_int(wait_res.type, ==, SP_OK);				\
									\
    RuntimeVariable *var = init_var(pc,					\
				    dbg.load_address,			\
				    (var_name),				\
				    dbg.pid,				\
				    dbg.info);				\
    assert_ptr_not_null(var);						\
    assert_true(is_addr_loc(var));					\
    real_addr loc_addr = var_loc_addr(var);				\
    del_var(var);							\
									\
    uint64_t value = 0;							\
    SprayResult mem_res = pt_read_memory(dbg.pid, loc_addr, &value);	\
    assert_int(mem_res, ==, SP_OK);					\
									\
    assert_int(value, ==, (expect));					\
									\
    del_debugger(dbg);							\
									\
    return MUNIT_OK;							\
  }

/* Stack variable declared in the function body. */
TEST_VARLOC(varloc_fbreg_works0, SIMPLE_64BIT_BIN, "a", 0x401163, 7)
/* Stack variable passed as a function parameter. */
TEST_VARLOC(varloc_fbreg_works1, RECURRING_VARIABLES_BIN, "c", 0x401124, 9)
/* Global variable. */
TEST_VARLOC(varloc_addr_works, RECURRING_VARIABLES_BIN, "a", 0x401124, 3)


extern SprayResult is_file_with_line(const char *file_line);

TEST(file_line_check_works) {
  SprayResult res = is_file_with_line("this/is/a/file:2578");
  assert_int(res, ==, SP_OK);

  res = is_file_with_line("this/is/a/filename/without/a/line");
  assert_int(res, ==, SP_ERR);

  res = is_file_with_line("710985");
  assert_int(res, ==, SP_ERR);

  res = is_file_with_line("src/blah/test.c74");
  assert_int(res, ==, SP_ERR);

  return MUNIT_OK;
}

extern SprayResult is_valid_identifier(const char *func_name);

TEST(function_name_check_works) {
  bool is_valid = is_valid_identifier("function_name_check_works1203");
  assert_true(is_valid);

  is_valid = is_valid_identifier("785019blah_function"); // Starts with numbers.
  assert_false(is_valid);

  is_valid = is_valid_identifier("check-function-name"); // Kebab case.
  assert_false(is_valid);

  is_valid = is_valid_identifier("check>function!>name"); // Other symbols.
  assert_false(is_valid);

  return MUNIT_OK;
}

MunitTest debugger_tests[] = {
    REG_TEST(breakpoints_work),
    REG_TEST(file_line_check_works),
    REG_TEST(function_name_check_works),
    REG_TEST(varloc_fbreg_works0),
    REG_TEST(varloc_fbreg_works1),
    REG_TEST(varloc_addr_works),
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};
