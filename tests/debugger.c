#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/source_files.h"

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


MunitTest debugger_tests[] = {
  REG_TEST(storing_files_works),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};
