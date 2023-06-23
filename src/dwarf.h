/* C wrapper around some features of libelfin */

#pragma once

#ifndef _SPRAY_DWARF_HH_
#define _SPRAY_DWARF_HH_

#ifdef __cplusplus
#define extern_c extern "C"
#else
#define extern_c
#endif  // __cplusplus

#include "ptrace.h"

/* All functions returning pointers return NULL
   if a C++ exception is raised internally. */

// Wrap `elf::elf`.
typedef void * w_elf_elf;

// Wrap `dwarf::dwarf`.
typedef void * w_dwarf_dwarf;

// Wrap `dwarf::die`.
typedef void * w_dwarf_die;

extern_c w_elf_elf elf_elf_ctor_mmap(int fd);
extern_c void      elf_elf_dtor(w_elf_elf w_elf);

extern_c w_dwarf_dwarf dwarf_dwarf_ctor_elf_loader(w_elf_elf w_elf);
extern_c void          dwarf_dwarf_dtor(w_dwarf_dwarf w_dwarf);

/* Get a DIE describing the function corresponding to the given PC. */
extern_c w_dwarf_die get_function_from_pc(w_dwarf_dwarf w_dwarf, x86_addr pc);
extern_c void        dwarf_die_dtor(w_dwarf_die w_die);

/* Returns `NULL` if the DIE doesn't  have a function name. */
extern_c const char *get_function_name_from_die(w_dwarf_die w_die);

#endif  // _SPRAY_DWARF_HH_
