#include "spray_elf.h"

#include "magic.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

typedef unsigned char byte;

/* IMPL NOTE:
The ELF header contains the file offset to the first byte of the program header
table and the first byte of the section header table. The entires in those tables
are the program headers or the section headers respectively. Those headers are
stored in the tables sequentially. The tables both contain `(e_phnum | e_phnum)` entires all
of which are `(e_phentsize | e_shentsize)` bytes in size.

"An object file segment contains one or more sections."
*/

/* For both 64-bit and 32-bit `elf.h` uses
   `uint16_t` to store the header table's
   offsets, their numbers of entires and the
   size of their individual entries. */

elf_parse_result parse_program_headers(
  byte *program_table_bytes,
  uint16_t n_program_headers,
  uint16_t program_header_size,
  ElfProgramHeader **program_headers
) {
  unused(program_table_bytes);
  unused(n_program_headers);
  unused(program_header_size);
  // No nothing.
  *program_headers = NULL;
  return ELF_PARSE_OK;
}

elf_parse_result parse_section_headers(
  void *section_table_bytes,
  uint16_t n_section_headers,
  uint16_t section_header_size,
  ElfSectionHeader **section_headers
) {
  unused(section_table_bytes);
  unused(n_section_headers);
  unused(section_header_size);
  *section_headers = NULL;
  return ELF_PARSE_OK;
}

elf_parse_result parse_elf(const char *filepath, ElfFile *elf_store) {
  assert(filepath != NULL);
  assert(elf_store != NULL);

  // Acquire file descriptor for `mmap`.
  int fd = open(filepath, O_RDONLY);
  if (fd == -1) {
    return ELF_PARSE_IO_ERR;
  }

  off_t n_bytes = lseek(fd, 0, SEEK_END);
  if (n_bytes == -1) {
    close(fd);
    return ELF_PARSE_IO_ERR;
  }

  byte *bytes = mmap(
      0, /* Kernel chooses address. */
      (size_t) n_bytes,  /* Init entire file. */
      PROT_READ,
      MAP_PRIVATE,
      fd,
      0);

  close(fd);  /* Close no matter the outcome of `mmap`. */

  if (bytes == MAP_FAILED) {
    return ELF_PARSE_IO_ERR;
  }

  /*** Parse the ELF header ***/

  Elf64_Ehdr elf_header;
  memcpy(&elf_header, bytes, sizeof(elf_header));

  // Is the magic number invalid?
  if  ((elf_header.e_ident[EI_MAG0] != ELFMAG0)  // 0x7f
    || (elf_header.e_ident[EI_MAG1] != ELFMAG1)  // 'E'
    || (elf_header.e_ident[EI_MAG2] != ELFMAG2)  // 'L'
    || (elf_header.e_ident[EI_MAG3] != ELFMAG3)  // 'F'
  ) {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_INVALID;
  }

  // Is this ELF file mean for something different than 64 bit?
  if (elf_header.e_ident[EI_CLASS] != ELFCLASS64) {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_DISLIKE;
  }

  // Is the file's data encoding two's complement and little-endian?
  if (elf_header.e_ident[EI_DATA] == ELFDATA2LSB) {
    elf_store->endianness = ELF_ENDIAN_LITTLE;
  }
  // Is the file's data encoding two's complement and big-endian?
  else if (elf_header.e_ident[EI_DATA] == ELFDATA2MSB) {
    elf_store->endianness = ELF_ENDIAN_BIG;
  }
  // Is the file's data encoding missing?
  else {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_DISLIKE;
  }

  // Is the ABI suppored? `ELFOSABI_NONE` is the same as `SYSV`.
  if  ((elf_header.e_ident[EI_OSABI] != ELFOSABI_LINUX)
    && (elf_header.e_ident[EI_OSABI] != ELFOSABI_NONE)
    && (elf_header.e_ident[EI_OSABI] != ELFOSABI_SYSV)
  ) {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_DISLIKE;
  }

  // `EI_VERSION` and `EI_ABIVERSION` are basically unused
  // and must conform to the values below to be valid.
  // `e_version` is the same.
  if (elf_header.e_ident[EI_VERSION] != EV_CURRENT
    || elf_header.e_ident[EI_ABIVERSION] != 0
    || elf_header.e_version != EV_CURRENT
  ) {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_INVALID;
  }

  // Is the object file type in the accepted range?
  if (elf_header.e_type <= ELF_TYPE_CORE) {
    // `e_type` maps to `elf_type` in this range.
    elf_store->type = (elf_type) elf_header.e_type;
  } else {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      // Object file type is in the reserved range.
      return ELF_PARSE_INVALID;
  }

  // Is the target instruction set architecture something
  // different than x86?  We're x86 only here!
  if (elf_header.e_machine != EM_X86_64) {
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return ELF_PARSE_DISLIKE;
  }

  /*** Parse program header table ***/

  elf_parse_result program_headers_res = parse_program_headers(
    bytes + elf_header.e_phoff,
    elf_header.e_phnum,
    elf_header.e_phentsize,
    &elf_store->program_headers
  );

  // Did we successfully parse all of the program headers?
  if (program_headers_res == ELF_PARSE_OK) {
    if (elf_store->program_headers == NULL)
      elf_store->n_program_headers = 0;
    else
      elf_store->n_program_headers = elf_header.e_phnum;
  } else {    
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return program_headers_res;
  }

  /*** Parse section header table ***/

  elf_parse_result section_headers_res = parse_section_headers(
    bytes + elf_header.e_shoff,
    elf_header.e_shnum,
    elf_header.e_shentsize,
    &elf_store->section_headers
  );

  // Did we successfully parse all of the section headers?
  if (section_headers_res == ELF_PARSE_OK) {
    if (elf_store->program_headers == NULL)
      elf_store->n_program_headers = 0;
    else
      elf_store->n_section_headers = elf_header.e_shnum;
  } else {    
    if (munmap(bytes, n_bytes) == -1)
      return ELF_PARSE_IO_ERR;
    else
      return section_headers_res;
  }

  if (munmap(bytes, n_bytes) == -1) {
    return ELF_PARSE_IO_ERR;
  }

  return ELF_PARSE_OK;
}


const char *elf_parse_result_name(elf_parse_result res) {
  static const char *elf_parse_result_names[] = {
    [ELF_PARSE_OK]="parsed file successfully",
    [ELF_PARSE_IO_ERR]="file I/O error",
    [ELF_PARSE_INVALID]="invalid file contents",
    [ELF_PARSE_DISLIKE]="unsupported file contents",
  };

  return elf_parse_result_names[res];
}
