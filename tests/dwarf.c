#include <libdwarf-0/libdwarf.h>

#include "test_utils.h"

#define UNIT_TESTS
#include "../src/info.h"
#include "../src/spray_dwarf.h"

#include <limits.h>
#include <stdlib.h>
#include <dwarf.h>

enum {
  RAND_DATA_BUF_SIZE = 32,
};

TEST(get_line_entry_from_pc_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  {  /* Happy path. */
    dbg_addr pc = { 0x00401156 };
    LineEntry line_entry = sd_line_entry_from_pc(dbg, pc);
    assert_true(line_entry.is_ok);
    assert_int(line_entry.ln, ==, 11);
    assert_int(line_entry.cl, ==, 7);
    assert_ptr_not_null(line_entry.filepath);
    /* Ignore the part of the filepath that is host specific. */
    assert_ptr_not_null(strstr(line_entry.filepath, SIMPLE_SRC));
  }
  {  /* Sad path 😢. */
    dbg_addr pc = { 0xdeabbeef };
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
                           SearchFor search_for,
                           SearchFindings search_findings
) {  
  assert(dbg != NULL);
  assert(die != NULL);

  const char *const fn_name = (char *) search_for.data;
  if (sd_is_subprog_with_name(dbg, die, fn_name)) {
    unsigned *level = (unsigned *) search_findings.data;
    *level = search_for.level;
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

  unsigned found_at_level = -1;	/* Not a valid level. */
  res = sd_search_dwarf_dbg(dbg,
                            &error,
                            callback__test_search,
                            "main",  // <- This does exist.
                            &found_at_level);
  assert_int(res, ==, DW_DLV_OK);
  assert_int(found_at_level, ==, 1);

  dwarf_finish(dbg);

  return MUNIT_OK;
}

SprayResult test_get_effective_start_addr(Dwarf_Debug dbg,
                                          const DebugSymbol *sym,
                                          dbg_addr *dest) {
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

  dbg_addr main_start = { 0 };
  SprayResult res = test_get_effective_start_addr(dbg, sym, &main_start);
  assert_int(res, ==, SP_OK);
  LineEntry line_entry = sd_line_entry_from_pc(dbg, main_start);
  assert_true(line_entry.is_ok);
  /* 10 is the line number of the first line after the function declaration. */
  assert_int(line_entry.ln, ==, 10);
  
  /* `weird_sum` has a multi-line function declaration. */
  sym = sym_by_name("weird_sum", info);
  dbg_addr func_start = { 0 };
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
    dbg_addr pc = { 0x00401156 };
    char *filepath = sd_filepath_from_pc(dbg, pc);
    assert_ptr_not_null(filepath);
    char *expect_filepath = realpath(SIMPLE_SRC, NULL);
    assert_string_equal(filepath, expect_filepath);
    free(filepath);
    free(expect_filepath);
  }
  {  /* Sad path. */
    dbg_addr pc = { 0xdeadbeef };
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

#define ASSERT_TYPE(name, pc, _type)                                           \
  {                                                                            \
    SdVarattr var_attr = {0};                                                  \
    char *unused_decl_file = NULL;                                             \
    unsigned unused_decl_line = 0;                                             \
    SprayResult res = sd_runtime_variable(                                     \
        dbg, (pc), (name), &var_attr, &unused_decl_file, &unused_decl_line);   \
    assert_int(res, ==, SP_OK);                                                \
    assert_int(var_attr.type.n_nodes, ==, (_type).n_nodes);                    \
    for (size_t i = 0; i < (_type).n_nodes; i++) {                             \
      assert_memory_equal(sizeof(*(_type).nodes), &(_type).nodes[i],           \
                          &var_attr.type.nodes[i]);                            \
    }                                                                          \
                                                                               \
    free(unused_decl_file);                                                    \
    del_type(&var_attr.type);						\
  }

TEST(finding_basic_variable_types_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(TYPE_EXAMPLES_BIN, &error);
  assert_ptr_not_null(dbg);

  /* There is no executable code in this CU. */
  dbg_addr addr = {0x0};
  
  SdTypenode a_nodes[1] = {
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_INT, .size = 4 }},
  };
  SdType a = { .n_nodes = 1, .nodes = (SdTypenode*)&a_nodes };
  ASSERT_TYPE("a", addr, a);

  SdTypenode b_nodes[2] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_CONST },
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_LONG, .size = 8 }}, 
  };
  SdType b = { .n_nodes = 2, .nodes = (SdTypenode*)&b_nodes };
  ASSERT_TYPE("b", addr, b);

  SdTypenode c_nodes[1] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_POINTER },
  };
  SdType c = { .n_nodes = 1, .nodes = (SdTypenode*)&c_nodes };
  ASSERT_TYPE("c", addr, c);
 
  SdTypenode d_nodes[2] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_POINTER },
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_LONG_LONG, .size = 8 }}, 
  };
  SdType d = { .n_nodes = 2, .nodes = (SdTypenode*)&d_nodes };
  ASSERT_TYPE("d", addr, d);
 
  SdTypenode e_nodes[3] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_POINTER },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_CONST },
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_UNSIGNED_INT, .size = 4 }}, 
  };
  SdType e = { .n_nodes = 3, .nodes = (SdTypenode*)&e_nodes };
  ASSERT_TYPE("e", addr, e);

  SdTypenode f_nodes[3] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_CONST },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_POINTER },
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_INT, .size = 4 }}, 
  };
  SdType f = { .n_nodes = 3, .nodes = (SdTypenode*)&f_nodes };
  ASSERT_TYPE("f", addr, f);

  SdTypenode g_nodes[6] = {
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_CONST },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_RESTRICT },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_POINTER },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_CONST },
    { .tag = NODE_MODIFIER, .modifier = TYPE_MOD_VOLATILE },
    { .tag = NODE_BASE_TYPE, .base_type = { .tag = BASE_TYPE_CHAR, .size = 1 }}, 
  };
  SdType g = { .n_nodes = 6, .nodes = (SdTypenode*)&g_nodes };
  ASSERT_TYPE("g", addr, g);

  dwarf_finish(dbg);
  return MUNIT_OK;
}

/*
 Assert that the first location description in the location list
 for the variable `name` in `func` has the given values.
*/
#define ASSERT_LOCDESC(name, pc, opcode_, op1, op2, op3, lowpc_, highpc_,      \
                       file)                                                   \
  {                                                                            \
    SdLoclist loclist = {0};                                                   \
    SdVarattr var_attr = {0};                                                  \
    char *decl_file = NULL;                                                    \
    unsigned decl_line = 0;                                                    \
    SprayResult res = sd_runtime_variable(dbg, (pc), (name), &var_attr,        \
                                          &decl_file, &decl_line);             \
    assert_int(res, ==, SP_OK);                                                \
    res = sd_init_loclist(dbg, var_attr.loc, &loclist);                        \
    assert_int(res, ==, SP_OK);                                                \
    assert_int(loclist.ranges[0].lowpc.value, ==, (lowpc_));                   \
    assert_int(loclist.ranges[0].highpc.value, ==, (highpc_));                 \
    assert_int(loclist.exprs[0].operations[0].opcode, ==, (opcode_));          \
    assert_int(loclist.exprs[0].operations[0].operand1, ==, (op1));            \
    assert_int(loclist.exprs[0].operations[0].operand2, ==, (op2));            \
    assert_int(loclist.exprs[0].operations[0].operand3, ==, (op3));            \
    assert_string_equal(decl_file, (file));                                    \
    free(decl_file);                                                           \
    del_type(&var_attr.type);                                                   \
    del_loclist(&loclist);                                                     \
  }

TEST(finding_variable_locations_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  dbg_addr main_addr = {0x401163}; /* Address from the binary's `main`. */
  char* file_path = realpath(SIMPLE_SRC, NULL);
  assert_ptr_not_null(file_path);

  ASSERT_LOCDESC("a", main_addr, DW_OP_fbreg, -8, 0, 0, 0, 0, file_path);

  free(file_path);
  dwarf_finish(dbg);
  return MUNIT_OK;
}

TEST(finding_locations_by_scope_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(RECURRING_VARIABLES_BIN, &error);
  assert_ptr_not_null(dbg);

  dbg_addr main_addr = {0x401182}; /* Some address in the binary's `main`. */
  dbg_addr blah_addr = {0x401132}; /* Some address in the `blah` function. */
  char* file_path = realpath(RECURRING_VARIABLES_SRC, NULL);
  assert_ptr_not_null(file_path);
  
  ASSERT_LOCDESC("a", main_addr, DW_OP_fbreg, -8, 0, 0, 0, 0, file_path);
  ASSERT_LOCDESC("b", main_addr, DW_OP_fbreg, -24, 0, 0, 0, 0, file_path);
  ASSERT_LOCDESC("c", main_addr, DW_OP_fbreg, -32, 0, 0, 0, 0, file_path);

  ASSERT_LOCDESC("a", blah_addr, DW_OP_addr, 4202512, 0, 0, 0, 0, file_path);
  ASSERT_LOCDESC("b", blah_addr, DW_OP_fbreg, -16, 0, 0, 0, 0, file_path);
  ASSERT_LOCDESC("c", blah_addr, DW_OP_fbreg, -24, 0, 0, 0, 0, file_path);

  free(file_path);
  dwarf_finish(dbg);

  return MUNIT_OK;
}

TEST(finding_variable_declration_files_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(EXTERN_VARIABLES_BIN, &error);
  assert_ptr_not_null(dbg);

  dbg_addr addr = {0x40115e};
  char *blah_int1_file = realpath("tests/assets/extern-variables/first_file.c", NULL);
  char *blah_int2_file = realpath("tests/assets/extern-variables/second_file.c", NULL);
  char *blah_int_another_file = realpath("tests/assets/extern-variables/third_file.c", NULL);
  char *my_own_int_file = realpath("tests/assets/extern-variables/main.c", NULL);
  assert_ptr_not_null(blah_int1_file);
  assert_ptr_not_null(blah_int2_file);
  assert_ptr_not_null(blah_int_another_file);
  assert_ptr_not_null(my_own_int_file);

  ASSERT_LOCDESC("blah_int1", addr, DW_OP_addr, 0x404014, 0, 0, 0, 0, blah_int1_file);
  ASSERT_LOCDESC("blah_int2", addr, DW_OP_addr, 0x404010, 0, 0, 0, 0, blah_int2_file);
  ASSERT_LOCDESC("blah_int_another", addr, DW_OP_addr, 0x404018, 0, 0, 0, 0, blah_int_another_file);
  ASSERT_LOCDESC("my_own_int", addr, DW_OP_addr, 0x40400c, 0, 0, 0, 0, my_own_int_file);

  free(blah_int1_file);
  free(blah_int2_file);
  free(blah_int_another_file);
  free(my_own_int_file);
  dwarf_finish(dbg);

  dbg = sd_dwarf_init(INCLUDE_VARIABLE_BIN, &error);
  assert_ptr_not_null(dbg);

  addr = (dbg_addr) {0x401129};
  char *blah_file = realpath("tests/assets/include-variable/header.h", NULL);
  char *here_file = realpath("tests/assets/include-variable/main.c", NULL);
  assert_ptr_not_null(blah_file);
  assert_ptr_not_null(here_file);
  
  ASSERT_LOCDESC("blah", addr, DW_OP_addr, 0x404004, 0, 0, 0, 0, blah_file);
  ASSERT_LOCDESC("here", addr, DW_OP_addr, 0x404008, 0, 0, 0, 0, here_file);

  free(blah_file);
  free(here_file);
  dwarf_finish(dbg);

  return MUNIT_OK;
}

TEST(manual_check_locexpr_output) {
  SdExpression first = {0};
  first.n_operations = 2;
  first.operations = calloc(first.n_operations, sizeof(SdOperation));
  first.operations[0] = (SdOperation) {
    .opcode = DW_OP_fbreg,	/* Has one operand. */
    .operands = {13, 0, 0},
  };
  first.operations[1] = (SdOperation) {
    .opcode = DW_OP_const_type,	/* Has three operands. */
    .operands = {14, 15, 16},
  };

  SdExpression second = {0};
  second.n_operations = 1;
  second.operations = calloc(second.n_operations, sizeof(SdOperation));
  second.operations[0] =  (SdOperation) {
    .opcode = DW_OP_deref_type,	/* Has two operands. */
    .operands = {123, 456},
  };

  SdLoclist loclist = {0};
  loclist.n_exprs = 2;
  loclist.exprs = calloc(loclist.n_exprs, sizeof(SdExpression));
  loclist.exprs[0] = first;
  loclist.exprs[1] = second;

  loclist.ranges = calloc(loclist.n_exprs, sizeof(SdLocRange));
  loclist.ranges[0] = (SdLocRange) {
    .meaningful = true,
    .lowpc = {78},
    .highpc = {910},
  };
  loclist.ranges[1] = (SdLocRange) {
    .meaningful = true,
    .lowpc = {11},
    .highpc = {12},
  };

  /* TODO: Replace this test with an integration test,
     that captures the output that's emitted here and
     checks that the output is correct. */

  printf("\n");			/* Initial newline for easier inspection. */
  print_loclist(loclist);
  del_loclist(&loclist);

  return MUNIT_OK;
}

TEST(validating_compilers_works) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(WRONG_COMPILER_BIN, &error);
  assert_ptr_equal(dbg, NULL);
  return MUNIT_OK;
}

TEST(type_attribute_form) {
  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init(SIMPLE_64BIT_BIN, &error);
  assert_ptr_not_null(dbg);

  SdVarattr var_attr = {0};
  dbg_addr main_addr = {0x401163}; /* Address from the binary's `main`. */
  char *decl_file = NULL;
  unsigned decl_line = 0;
  SprayResult res = sd_runtime_variable(dbg,
					main_addr,
				        "a",
					&var_attr,
					&decl_file,
					&decl_line);
  assert_int(res, ==, SP_OK);

  dwarf_finish(dbg);
  del_type(&var_attr.type);
  free(decl_file);

  return MUNIT_OK;
}


MunitTest dwarf_tests[] = {
    REG_TEST(get_line_entry_from_pc_works),
    REG_TEST(iterating_lines_works),
    REG_TEST(search_returns_the_correct_result),
    REG_TEST(get_effective_function_start_works),
    REG_TEST(get_filepath_from_pc_works),
    REG_TEST(sd_line_entry_at_works),
    REG_TEST(finding_basic_variable_types_works),
    REG_TEST(finding_variable_locations_works),
    REG_TEST(finding_locations_by_scope_works),
    REG_TEST(manual_check_locexpr_output),
    REG_TEST(finding_variable_declration_files_works),
    REG_TEST(validating_compilers_works),
    REG_TEST(type_attribute_form),
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};
