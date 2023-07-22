#include <elf.h>
#define MUNIT_ENABLE_ASSERT_ALIASES
#include "munit.h"

#include "../src/spray_elf.h"

TEST(accept_valid_executable) {
  const char *filepath = "tests/assets/linux_x86_bin";
  ElfFile elf_file = {0};
  ElfParseResult res = parse_elf(filepath, &elf_file);
  assert_int(res, ==, ELF_PARSE_OK);

  assert_int(elf_file.prog_table.n_headers, ==, 13);
  assert_int(elf_file.sect_table.n_headers, ==, 33);

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

  Elf64_Shdr symtab_sh = elf_file.sect_table.headers[30];
  assert_int(symtab_sh.sh_type, ==, SHT_SYMTAB);
  assert_int(symtab_sh.sh_addr, ==, 0x0);
  assert_int(symtab_sh.sh_offset, ==, 0x49d8);
  assert_int(symtab_sh.sh_size, ==, 0x630);
  assert_int(symtab_sh.sh_entsize, ==, 0x18);
  assert_int(symtab_sh.sh_flags, ==, 0);
  assert_int(symtab_sh.sh_link, ==, 31);
  assert_int(symtab_sh.sh_info, ==, 50);
  assert_int(symtab_sh.sh_addralign, ==, 8);

  free_elf(elf_file);
  return MUNIT_OK;
}

TEST(reject_invalid_executables) {
  // The following are a buch of executables which
  // were compiled for unsupported targets (32-bit, ARM etc.)
  // All of them should be rejects.

  // 32 bit binary.
  const char *filepath = "tests/assets/linux_32_bin";
  ElfFile elf_file = {0};
  ElfParseResult res = parse_elf(filepath, &elf_file);
  assert_int(res, ==, ELF_PARSE_DISLIKE);
  return MUNIT_OK;
}

MunitTest parse_elf_tests[] = {
  REG_TEST(accept_valid_executable),
  REG_TEST(reject_invalid_executables),
  { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }  
};
