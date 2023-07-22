/* Parse ELF files and provide relevant info. */

#pragma once

#ifndef _SPARY_SPRAY_ELF_H_
#define _SPRAY_SPRAY_ELF_H_

#define _GNU_SOURCE

#include "magic.h"

#include <stdlib.h>
#include <stdint.h>
#include <elf.h>

typedef unsigned char byte;

typedef enum {
  ELF_TYPE_NONE = ET_NONE,
  ELF_TYPE_REL  = ET_REL,
  ELF_TYPE_EXEC = ET_EXEC,
  ELF_TYPE_DYN  = ET_DYN,
  ELF_TYPE_CORE = ET_CORE,
} ElfType;

typedef enum {
  ELF_ENDIAN_BIG,
  ELF_ENDIAN_LITTLE,
} Endianness;

// ELF program header table.
typedef struct {
  uint32_t n_headers;
  Elf64_Phdr *headers;
} ElfProgTable;

// ELF section header table.
typedef struct {
  uint32_t n_headers;
  // Index of section name string table in `headers`.
  uint32_t name_idx;
  Elf64_Shdr *headers;
} ElfSectTable;

typedef struct {
  // Privately memory mapped content of file.
  byte *bytes;
  size_t n_bytes;
} ElfData;

typedef struct {
  ElfType type;
  Endianness endianness;
  ElfProgTable prog_table;
  ElfSectTable sect_table;
  ElfData data;
} ElfFile;

typedef enum {
  ELF_PARSE_OK,
  ELF_PARSE_IO_ERR,   /* Error during I/O. */
  ELF_PARSE_INVALID,  /* Invalid file. */
  ELF_PARSE_DISLIKE,  /* Theoretically a valid ELF file but
                         some feature used is not suppored. */
} ElfParseResult;

const char *elf_parse_result_name(ElfParseResult res);

/* Parse an ELF file and store the info in `elf`.
   Returns `ELF_PARSE_OK` on success. `*elf` might
   be changed even if the result is ultimately an error. */
ElfParseResult parse_elf(const char *filepath, ElfFile *elf);

// Returns `SP_ERR` if unmapping the ELF file didn't work.
SprayResult free_elf(ElfFile elf);

#endif  // _SPRAY_PARSE_ELF_H_
