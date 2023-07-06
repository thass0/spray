#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/source_files.h"
#include "../src/breakpoints.h"
#include "../src/debugger.h"

#define FILE_PATH "tests/assets/debug_me.c"

TEST(storing_files_works) {
  SourceFiles *source_files = init_source_files();
  assert_ptr_not_null(source_files);

  SourceLines lookup = { .filepath=FILE_PATH };
  assert_ptr_equal(NULL, hashmap_get(source_files, &lookup));

  freopen("/dev/null", "w", stdout);
  print_source(source_files, FILE_PATH, 2, 2);
  
  /* Internally `print_source` will use this call to `hashmap_get`
     to determine if it should read the file again. If this call
     returns a non-null pointer, then the file won't be read again. */
  assert_ptr_not_null(hashmap_get(source_files, &lookup));
  free_source_files(source_files);
  
  return MUNIT_OK;
}

TEST(breakpoints_work) {
  Debugger dbg;
  assert_int(setup_debugger("tests/assets/linux_x86_bin", &dbg), ==, 0);

  x86_addr bp_addr1 = { 0x00401122 };

  enable_breakpoint(dbg.breakpoints, bp_addr1);
  assert_true(lookup_breakpoint(dbg.breakpoints, bp_addr1));

  disable_breakpoint(dbg.breakpoints, bp_addr1);
  assert_false(lookup_breakpoint(dbg.breakpoints, bp_addr1));

  free_debugger(dbg);
  
  return MUNIT_OK;
}


MunitTest debugger_tests[] = {
  REG_TEST(storing_files_works),
  REG_TEST(breakpoints_work),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};
