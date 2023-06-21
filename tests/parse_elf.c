#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/parse_elf.h"

#include "stdio.h"

TEST(accept_valid_executable) {
  const char *filepath = "tests/assets/linux_x86_bin";
  ElfFile elf_file = {0};
  elf_parse_result res = parse_elf(filepath, &elf_file);
  assert_int(res, ==, ELF_PARSE_OK);
  return MUNIT_OK;
}

TEST(reject_invalid_executables) {
  // The following are a buch of executables which
  // were compiled for unsupported targets (32-bit, ARM etc.)
  // All of them should be rejects.

  // 32 bit Intel 80386 binary.
  const char *filepath = "tests/assets/linux_i386_bin";
  ElfFile elf_file = {0};
  elf_parse_result res = parse_elf(filepath, &elf_file);
  assert_int(res, ==, ELF_PARSE_DISLIKE);
  return MUNIT_OK;
}

MunitTest parse_elf_tests[] = {
  REG_TEST(accept_valid_executable),
  REG_TEST(reject_invalid_executables),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};
