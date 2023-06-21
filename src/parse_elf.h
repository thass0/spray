/* Parse ELF files and provide relevant info. */

#pragma once

#ifndef _SPARY_PARSE_ELF_H_
#define _SPRAY_PARSE_ELF_H_

#define _GNU_SOURCE

#include <stdint.h>
#include <elf.h>

typedef enum {
  ELF_TYPE_NONE = ET_NONE,
  ELF_TYPE_REL  = ET_REL,
  ELF_TYPE_EXEC = ET_EXEC,
  ELF_TYPE_DYN  = ET_DYN,
  ELF_TYPE_CORE = ET_CORE,
} elf_type;

typedef enum {
  ELF_ENDIAN_BIG,
  ELF_ENDIAN_LITTLE,
} elf_endianness;

typedef struct {
  int blah;
} ElfProgramHeader;

typedef struct {
  int blah;
} ElfSectionHeader;

typedef struct {
  elf_type type;
  elf_endianness endianness;
  ElfProgramHeader *program_headers;
  uint16_t n_program_headers;
  ElfSectionHeader *section_headers;
  uint16_t n_section_headers;
} ElfFile;

typedef enum {
  ELF_PARSE_OK,
  ELF_PARSE_IO_ERR,  /* Error during I/O. */
  ELF_PARSE_INVALID,  /* Invalid file. */
  ELF_PARSE_DISLIKE,  /* Technically a valid ELF
                         file but it's not suppored. */
} elf_parse_result;

const char *elf_parse_result_name(elf_parse_result res);

/* Parse an ELF file and store the info in `elf_store`.
   Returns `ELF_PARSE_OK` on success. `*elf_store` might
   be changed even if the result is ultimately an error. */
elf_parse_result parse_elf(const char *filepath, ElfFile *elf_store);

#endif  // _SPRAY_PARSE_ELF_H_
