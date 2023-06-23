#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/dwarf.h"

#include <fcntl.h>

TEST(libelfin_ffi_works) {
  // Check the elf::elf ctor works.
  int fd = open("tests/assets/linux_x86_bin", O_RDONLY);
  w_elf_elf w_elf = elf_elf_ctor_mmap(fd);
  assert_ptr_not_null(w_elf);

  // Check that dwarf::dwarf ctor works.
  w_dwarf_dwarf w_dwarf = dwarf_dwarf_ctor_elf_loader(w_elf);
  assert_ptr_not_null(w_dwarf);

  dwarf_dwarf_dtor(w_dwarf);
  elf_elf_dtor(w_elf);
  return MUNIT_OK;
}

TEST(pc_to_function_works) {
  /*
  We can find the value of the main function's PC using `dwarfdump`:
  DW_TAG_subprogram
                      [...]
                      DW_AT_name                  main
                      [...]
                      DW_AT_low_pc                0x00401126
                      DW_AT_high_pc               <offset-from-lowpc> 41 <highpc: 0x0040114f>
  */
  x86_addr pc = { 0x0040112a };

  int fd = open("tests/assets/linux_x86_bin", O_RDONLY);
  w_elf_elf w_elf = elf_elf_ctor_mmap(fd);
  assert_ptr_not_null(w_elf);
  w_dwarf_dwarf w_dwarf = dwarf_dwarf_ctor_elf_loader(w_elf);
  assert_ptr_not_null(w_dwarf);

  w_dwarf_die function_die = get_function_from_pc(w_dwarf, pc);
  assert_ptr_not_null(function_die);
  const char *function_name = get_function_name_from_die(function_die);
  assert_ptr_not_null(function_name);
  assert_string_equal(function_name, "main");

  return MUNIT_OK;
}

MunitTest libelfin_wrapper_tests[] = {
  REG_TEST(libelfin_ffi_works),
  REG_TEST(pc_to_function_works),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};
