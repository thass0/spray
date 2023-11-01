#include <elf.h>

#include "test_utils.h"

#include "../src/spray_elf.h"

TEST(accept_valid_executable) {
  ElfFile elf_file = {0};
  ElfParseResult res = se_parse_elf(SIMPLE_64BIT_BIN, &elf_file);
  assert_int(res, ==, ELF_PARSE_OK);

  assert_int(elf_file.prog_table.n_headers, ==, 13);
  assert_int(elf_file.sect_table.n_headers, ==, 34);

  // Compare some randomly chosen values to those
  // returned by `readelf(1)`.

  assert_int(elf_file.prog_table.headers[0].p_type, ==, PT_PHDR);

  Elf64_Phdr load_ph = elf_file.prog_table.headers[3];
  assert_int(load_ph.p_type, ==, PT_LOAD);
  assert_int(load_ph.p_offset, ==, 0x1000);
  assert_int(load_ph.p_vaddr, ==, 0x401000);
  assert_int(load_ph.p_paddr, ==, 0x401000);
  assert_int(load_ph.p_filesz, ==, 0x181);
  assert_int(load_ph.p_memsz, ==, 0x181);
  assert_int(load_ph.p_flags, ==, PF_R | PF_X);
  assert_int(load_ph.p_align, ==, 0x1000);

  Elf64_Phdr eh_frame_ph = elf_file.prog_table.headers[10];
  assert_int(eh_frame_ph.p_type, ==, PT_GNU_EH_FRAME);
  assert_int(eh_frame_ph.p_offset, ==, 0x2010);
  assert_int(eh_frame_ph.p_vaddr, ==, 0x402010);
  assert_int(eh_frame_ph.p_paddr, ==, 0x402010);
  assert_int(eh_frame_ph.p_filesz, ==, 0x2c);
  assert_int(eh_frame_ph.p_memsz, ==, 0x2c);
  assert_int(eh_frame_ph.p_flags, ==, PF_R);
  assert_int(eh_frame_ph.p_align, ==, 0x4);

  Elf64_Shdr symtab_sh = elf_file.sect_table.headers[31];
  assert_int(symtab_sh.sh_type, ==, SHT_SYMTAB);
  assert_int(symtab_sh.sh_addr, ==, 0x0);
  assert_int(symtab_sh.sh_offset, ==, 13656);
  assert_int(symtab_sh.sh_size, ==, 0x330);
  assert_int(symtab_sh.sh_entsize, ==, 0x18);
  assert_int(symtab_sh.sh_flags, ==, 0);
  assert_int(symtab_sh.sh_link, ==, 32);
  assert_int(symtab_sh.sh_info, ==, 18);
  assert_int(symtab_sh.sh_addralign, ==, 8);

  se_free_elf(elf_file);
  return MUNIT_OK;
}

TEST(read_elf_symbol_table_entries) {
  ElfFile elf_file = {0};
  ElfParseResult res = se_parse_elf(MULTI_FILE_BIN, &elf_file);
  assert_int(res, ==, ELF_PARSE_OK);

  const Elf64_Sym *main = se_symbol_from_name("main", &elf_file);
  assert_ptr_not_null(main);
  assert_string_equal(se_symbol_name(main, &elf_file), "main");
  assert_int(se_symbol_binding(main), ==, STB_GLOBAL);
  assert_int(se_symbol_type(main), ==, STT_FUNC);
  assert_int(se_symbol_visibility(main), ==, STV_DEFAULT);

  const Elf64_Sym *func2 =
      se_symbol_from_name("file2_compute_something", &elf_file);
  assert_ptr_not_null(func2);
  assert_string_equal(se_symbol_name(func2, &elf_file),
                      "file2_compute_something");
  assert_int(se_symbol_binding(func2), ==, STB_GLOBAL);
  assert_int(se_symbol_type(func2), ==, STT_FUNC);
  assert_int(se_symbol_visibility(func2), ==, STV_DEFAULT);

  const Elf64_Sym *func1 =
      se_symbol_from_addr((dbg_addr){0x00401128}, &elf_file);
  assert_ptr_not_null(func1);
  assert_string_equal(se_symbol_name(func1, &elf_file),
                      "file1_compute_something");
  assert_int(se_symbol_binding(func1), ==, STB_GLOBAL);
  assert_int(se_symbol_type(func1), ==, STT_FUNC);
  assert_int(se_symbol_visibility(func1), ==, STV_DEFAULT);

  se_free_elf(elf_file);
  return MUNIT_OK;
}

TEST(reject_invalid_executables) {
  // The following are a bunch of executables which
  // were compiled for unsupported targets (32-bit, ARM etc.)
  // All of them should be rejects.

  ElfFile elf_file = {0};
  ElfParseResult res = se_parse_elf(SIMPLE_32BIT_BIN, &elf_file);
  assert_int(res, ==, ELF_PARSE_DISLIKE);
  return MUNIT_OK;
}

MunitTest parse_elf_tests[] = {
    REG_TEST(accept_valid_executable),
    REG_TEST(reject_invalid_executables),
    REG_TEST(read_elf_symbol_table_entries),
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};
