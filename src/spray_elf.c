#include "spray_elf.h"

#include "magic.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum
{
  CHECK_SECTION_HEADER = 0xffff,
};

/* Validates the content of the given `Elf64_Ehdr` and
 * parses all values of interest. Some values (`n_prog_hdrs`,
 * `n_sect_hdrs` and `shstrtab_idx`) might be too
 * large to be stored in the `Elf64_Ehdr`. Then they are set to
 * `CHECK_SECTION_HEADER` to signal that they should be read from
 * the inital section header. */
ElfParseResult
parse_elf_header (const Elf64_Ehdr *elf_src,
		  ElfFile *elf_dest,
		  uint64_t *prog_table_off,
		  uint64_t *sect_table_off,
		  uint32_t *n_prog_hdrs,
		  uint32_t *n_sect_hdrs, uint32_t *shstrtab_idx)
{
  assert (elf_dest != NULL);
  assert (prog_table_off != NULL);
  assert (sect_table_off != NULL);
  assert (n_prog_hdrs != NULL);
  assert (n_sect_hdrs != NULL);
  assert (shstrtab_idx != NULL);

  /* Is the magic number invalid? */
  if ((elf_src->e_ident[EI_MAG0] != ELFMAG0)	/* 0x7f */
      || (elf_src->e_ident[EI_MAG1] != ELFMAG1)	/* 'E'  */
      || (elf_src->e_ident[EI_MAG2] != ELFMAG2)	/* 'L'  */
      || (elf_src->e_ident[EI_MAG3] != ELFMAG3)	/* 'F'  */
    )
    {
      return ELF_PARSE_INVALID;
    }

  /* Is this ELF file meant for something different than 64 bit? */
  if (elf_src->e_ident[EI_CLASS] != ELFCLASS64)
    {
      return ELF_PARSE_DISLIKE;
    }

  /* Is the file's data encoding two's complement and little-endian? */
  if (elf_src->e_ident[EI_DATA] == ELFDATA2LSB)
    {
      elf_dest->endianness = ELF_ENDIAN_LITTLE;
    }
  /* Is the file's data encoding two's complement and big-endian? */
  else if (elf_src->e_ident[EI_DATA] == ELFDATA2MSB)
    {
      elf_dest->endianness = ELF_ENDIAN_BIG;
    }
  /* Is the file's data encoding missing? */
  else
    {
      return ELF_PARSE_DISLIKE;
    }

  /* Is the ABI suppored? `ELFOSABI_NONE` is the same as `SYSV`. */
  if ((elf_src->e_ident[EI_OSABI] != ELFOSABI_LINUX)
      && (elf_src->e_ident[EI_OSABI] != ELFOSABI_NONE)
      && (elf_src->e_ident[EI_OSABI] != ELFOSABI_SYSV))
    {
      return ELF_PARSE_DISLIKE;
    }

  /* `EI_VERSION` and `EI_ABIVERSION` are basically unused
   * and must conform to the values below to be valid.
   * `e_version` is the same. */
  if (elf_src->e_ident[EI_VERSION] != EV_CURRENT
      || elf_src->e_ident[EI_ABIVERSION] != 0
      || elf_src->e_version != EV_CURRENT)
    {
      return ELF_PARSE_INVALID;
    }

  /* Is the object file type in the accepted range? */
  if (elf_src->e_type <= ELF_TYPE_CORE)
    {
      /* `e_type` maps to `elf_type` in this range. */
      elf_dest->type = (ElfType) elf_src->e_type;
    }
  else
    {
      /* Object file type is in the reserved range. */
      return ELF_PARSE_INVALID;
    }

  /* Is the target instruction set architecture something
   * different than x86?  We're x86 only here! */
  if (elf_src->e_machine != EM_X86_64)
    {
      return ELF_PARSE_DISLIKE;
    }


  /*********************************************/
  /* Program and section header table parsing. */
  /*********************************************/
  
  /* Is this file missing a program header table? */
  if (elf_src->e_phoff == 0)
    {
      return ELF_PARSE_DISLIKE;
    }
  else
    {
      *prog_table_off = elf_src->e_phoff;
    }

  /* Is this file missing a section header table? */
  if (elf_src->e_shoff == 0)
    {
      return ELF_PARSE_DISLIKE;
    }
  else
    {
      *sect_table_off = elf_src->e_shoff;
    }

  /* Are the entry sizes in the header tables meant for 64-bit? */
  if (elf_src->e_phentsize != sizeof (Elf64_Phdr) ||
      elf_src->e_shentsize != sizeof (Elf64_Shdr))
    {
      return ELF_PARSE_DISLIKE;
    }

  /* Some of the values in the ELF header don't fit
   * its data types anymore. E.g. if there are more
   * than 0xffff program headers, the `e_phnum` field
   * cannot store how many of them there are. In this
   * case, the first entry in the section header table
   * stores the actual real value. */

  /* Does the number of program headers exceed the representable range? */
  if (elf_src->e_phnum == PN_XNUM)
    {
      *n_prog_hdrs = CHECK_SECTION_HEADER;
    }
  else
    {
      *n_prog_hdrs = elf_src->e_phnum;
    }

  if (elf_src->e_shnum == 0)
    {
      /* `e_shnum` being 0 signals one of two options:
       * (1) The number of section table headers lies outside
       *     the range that can be represented in 16 bits and
       *     the actual value is found in `sh_size`.
       * (2) The number of entries in really just 0. Then `sh_size`
       *     will be 0, too. */
      *n_sect_hdrs = CHECK_SECTION_HEADER;
    }
  else
    {
      *n_sect_hdrs = elf_src->e_shnum;
    }

  /* Is the index of the section name string table outside
   * the range that can be represented? */
  if (elf_src->e_shstrndx == SHN_XINDEX)
    {
      *shstrtab_idx = CHECK_SECTION_HEADER;
    }
  else
    {
      *shstrtab_idx = elf_src->e_shstrndx;
    }

  return ELF_PARSE_OK;
}

/* The initial section header is reserved to store values that
 * didn't fit into the ELF header. If any of the argument's values
 * is set to `CHECK_SECTION_HEADER`, then it will be set to the
 * value in this header. */
void
parse_init_section (const Elf64_Shdr *init_section_header,
		    uint32_t *n_prog_hdrs, uint32_t *n_sect_hdrs,
		    uint32_t *shstrtab_idx)
{
  assert (init_section_header != NULL);
  assert (n_prog_hdrs != NULL);
  assert (n_sect_hdrs != NULL);
  assert (shstrtab_idx != NULL);

  if (*n_prog_hdrs == CHECK_SECTION_HEADER)
    {
      *n_prog_hdrs = init_section_header->sh_info;
    }

  if (*n_sect_hdrs == CHECK_SECTION_HEADER)
    {
      *n_sect_hdrs = init_section_header->sh_size;
    }

  if (*shstrtab_idx == CHECK_SECTION_HEADER)
    {
      *shstrtab_idx = init_section_header->sh_link;
    }
}

/* Helpers to check bit masks. */
bool
is_set (int value, int mask)
{
  return (value & mask) != 0;
}

bool
is_unset (int value, int mask)
{
  return (value & mask) == 0;
}

bool
is_valid_symtab (Elf64_Shdr *shdr, const char *name)
{
  return str_eq (name, ".symtab") && shdr->sh_type == SHT_SYMTAB &&
    /* `SHF_ALLOC` is always set for .dynsym. */
    is_unset (shdr->sh_flags, SHF_ALLOC) &&
    shdr->sh_entsize == sizeof (Elf64_Sym);
}

bool
is_valid_strtab (Elf64_Shdr *shdr, const char *name)
{
  return str_eq (name, ".strtab")
    && shdr->sh_type == SHT_STRTAB
    && is_unset (shdr->sh_flags, SHF_ALLOC);
}

SprayResult
find_table_sections (Elf64_Shdr *sect_headers, uint32_t n_sect_hdrs,
		     const char *shstrtab, uint32_t *symtab_idx,
		     uint32_t *strtab_idx)
{
  assert (sect_headers != NULL);
  assert (shstrtab != NULL);
  assert (symtab_idx != NULL);
  assert (strtab_idx != NULL);

  /* NOTE: To check if a given index has been set already,
   * we can check if it is zero. This relies on the fact
   * that the section header at index zero is reserved and
   * cannot be used for any of the entries we are looking for. */

  Elf64_Shdr *cur_hdr = NULL;
  const char *name = NULL;
  for (uint32_t i = 0; i < n_sect_hdrs; i++)
    {
      cur_hdr = &sect_headers[i];
      name = &shstrtab[cur_hdr->sh_name];
      if (is_valid_symtab (cur_hdr, name)
	  && *symtab_idx == 0)
	{
	  *symtab_idx = i;
	}
      else if (is_valid_strtab (cur_hdr, name)
	       && *strtab_idx == 0)
	{
	  *strtab_idx = i;
	}
    }

  if (*symtab_idx != 0 && *strtab_idx != 0)
    {
      return SP_OK;
    }
  else
    {
      return SP_ERR;
    }
}

SprayResult
file_size (int fd, size_t *dest)
{
  assert (dest != NULL);

  off_t n_bytes = lseek (fd, 0, SEEK_END);
  if (n_bytes < 0)
    {
      return SP_ERR;
    }
  else
    {
      *dest = (size_t) n_bytes;
      return SP_OK;
    }
}

/* Cast pointers pointing into the memory mapped ELF
 * file to specific structures. Using these functions
 * is much more readable than plain casts. */

static inline Elf64_Ehdr *
ehdr_at (byte *bytes, size_t off)
{
  return (Elf64_Ehdr *) (bytes + off);
}

static inline Elf64_Phdr *
phdr_at (byte *bytes, size_t off)
{
  return (Elf64_Phdr *) (bytes + off);
}

static inline Elf64_Shdr *
shdr_at (byte *bytes, size_t off)
{
  return (Elf64_Shdr *) (bytes + off);
}

static inline Elf64_Sym *
symtab_at (byte *bytes, size_t off)
{
  return (Elf64_Sym *) (bytes + off);
}

static inline char *
strtab_at (byte *bytes, size_t off)
{
  return (char *) (bytes + off);
}

ElfParseResult
se_parse_elf (const char *filepath, ElfFile *elf_store)
{
  assert (filepath != NULL);
  assert (elf_store != NULL);

  /* Acquire file descriptor for `mmap`. */
  int fd = open (filepath, O_RDONLY);
  if (fd == -1)
    {
      return ELF_PARSE_IO_ERR;
    }

  /* Get the number of bytes in the file. */
  size_t n_bytes = 0;
  if (file_size (fd, &n_bytes) == SP_ERR)
    {
      close (fd);
      return ELF_PARSE_IO_ERR;
    }

  byte *bytes = mmap (0,	/* Kernel chooses address. */
		      n_bytes,	/* Init the entire file. */
		      PROT_READ,
		      MAP_PRIVATE,
		      fd,
		      0);

  close (fd);			/* Close no matter the outcome of `mmap`. */

  if (bytes == MAP_FAILED)
    {
      return ELF_PARSE_IO_ERR;
    }

  /* Parse relevant information from the ELF header. */

  Elf64_Ehdr *elf_header = ehdr_at (bytes, 0);

  uint64_t prog_table_off = 0;
  uint32_t n_prog_hdrs = 0;

  uint64_t sect_table_off = 0;
  uint32_t n_sect_hdrs = 0;

  uint32_t shstrtab_idx = 0;

  ElfParseResult elf_header_res =
    parse_elf_header (elf_header, elf_store, &prog_table_off, &sect_table_off,
		      &n_prog_hdrs, &n_sect_hdrs, &shstrtab_idx);

  if (elf_header_res != ELF_PARSE_OK)
    {
      if (munmap (bytes, n_bytes) == -1)
	{
	  return ELF_PARSE_IO_ERR;
	}
      else
	{
	  return elf_header_res;
	}
    }

  Elf64_Shdr *sect_headers = shdr_at (bytes, sect_table_off);

  /* Fill-in missing values if they weren't found in the ELF header. */
  parse_init_section (sect_headers, &n_prog_hdrs, &n_sect_hdrs,
		      &shstrtab_idx);


  /* Find the section headers for the symbol table and the string table. */
  uint32_t symtab_idx = 0;
  uint32_t strtab_idx = 0;
  /* Get the section header string table that contains the names of
   * the sections in the section header table. `sh_name` is an index into
   * that table, and thus the table can be used to read the names of the
   * different sections. */
  Elf64_Shdr *shstrtab_hdr = &sect_headers[shstrtab_idx];
  const char *shstrtab = strtab_at (bytes, shstrtab_hdr->sh_offset);

  SprayResult tables_res = find_table_sections (sect_headers, n_sect_hdrs,
						shstrtab, &symtab_idx,
						&strtab_idx);

  if (tables_res == SP_ERR)
    {
      if (munmap (bytes, n_bytes) == -1)
	{
	  return ELF_PARSE_IO_ERR;
	}
      else
	{
	  return ELF_PARSE_INVALID;
	}
    }

  elf_store->sect_table = (ElfSectTable)
  {
  .n_headers = n_sect_hdrs,.symtab_idx = symtab_idx,.shstrtab_idx =
      shstrtab_idx,.strtab_idx = strtab_idx,.headers = sect_headers,};

  Elf64_Phdr *prog_headers = phdr_at (bytes, prog_table_off);
  elf_store->prog_table = (ElfProgTable)
  {
  .n_headers = n_prog_hdrs,.headers = prog_headers,};

  elf_store->data = (ElfData)
  {
  .bytes = bytes,.n_bytes = n_bytes,};

  return ELF_PARSE_OK;
}

const char *
elf_parse_result_name (ElfParseResult res)
{
  static const char *elf_parse_result_names[] = {
    [ELF_PARSE_OK] = "parsed file successfully",
    [ELF_PARSE_IO_ERR] = "file I/O error",
    [ELF_PARSE_INVALID] = "invalid file contents",
    [ELF_PARSE_DISLIKE] = "unsupported file contents",
  };

  return elf_parse_result_names[res];
}

SprayResult
se_free_elf (ElfFile elf)
{
  if (munmap (elf.data.bytes, elf.data.n_bytes) == -1)
    {
      return SP_ERR;
    }
  else
    {
      return SP_OK;
    }
}

const Elf64_Sym *
se_symbol_from_name (const char *name, const ElfFile *elf)
{
  assert (name != NULL);
  assert (elf != NULL);

  Elf64_Shdr *symtab_hdr =
    &elf->sect_table.headers[elf->sect_table.symtab_idx];
  const Elf64_Sym *symtab =
    symtab_at (elf->data.bytes, symtab_hdr->sh_offset);

  uint64_t n_symbols = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

  for (uint64_t i = 0; i < n_symbols; i++)
    {
      if (str_eq (se_symbol_name (&symtab[i], elf), name))
	{
	  return &symtab[i];
	}
    }

  return NULL;
}

const Elf64_Sym *
se_symbol_from_addr (dbg_addr addr, const ElfFile *elf)
{
  assert (elf != NULL);

  Elf64_Shdr *symtab_hdr =
    &elf->sect_table.headers[elf->sect_table.symtab_idx];
  const Elf64_Sym *symtab =
    symtab_at (elf->data.bytes, symtab_hdr->sh_offset);

  uint64_t n_symbols = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

  for (uint64_t i = 0; i < n_symbols; i++)
    {
      if (se_symbol_start_addr (&symtab[i]).value <= addr.value &&
	  se_symbol_end_addr (&symtab[i]).value >= addr.value)
	{
	  return &symtab[i];
	}
    }

  return NULL;
}

int
se_symbol_binding (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  return ELF64_ST_BIND (sym->st_info);
}

int
se_symbol_type (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  return ELF64_ST_TYPE (sym->st_info);
}

int
se_symbol_visibility (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  return sym->st_other;
}

uint64_t
symbol_value (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  return sym->st_value;
}

dbg_addr
se_symbol_start_addr (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  return (dbg_addr)
  {
  sym->st_value};
}

dbg_addr
se_symbol_end_addr (const Elf64_Sym *sym)
{
  assert (sym != NULL);
  /* The symbol's size is the offset from the
   * start address if the symbol is a function. */
  return (dbg_addr) { sym->st_value + sym->st_size };
}

const char *
se_symbol_name (const Elf64_Sym *sym, const ElfFile *elf)
{
  assert (sym != NULL);
  assert (elf != NULL);

  Elf64_Shdr *strtab_hdr =
    &elf->sect_table.headers[elf->sect_table.strtab_idx];
  const char *strtab = strtab_at (elf->data.bytes, strtab_hdr->sh_offset);
  return &strtab[sym->st_name];
}
