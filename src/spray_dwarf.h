/* Spray's wrapper around libdwarf. */

#pragma once

#ifndef _SPRAY_SPRAY_DWARF_H_
#define _SPRAY_SPRAY_DWARF_H_

#include "ptrace.h"

#include <libdwarf-0/libdwarf.h>
#include <stdbool.h>

/* Initialized debug info. Returns NULL on error. */
Dwarf_Debug sd_dwarf_init(const char *filepath, Dwarf_Error *error);

/* Get the filepath of the file while the given PC is part of.
   The strings that's returned must be free' by the caller. */
char *sd_filepath_from_pc(Dwarf_Debug dbg, x86_addr pc);

typedef struct {
  bool is_ok;
  bool new_statement;
  bool prologue_end;
  // Set to true if the PC used to retrieve the
  // line entry was exactly equal to `addr`.
  bool is_exact;
  unsigned ln;
  unsigned cl;
  x86_addr addr;
  // Don't free this string. It's owned by the `Dwarf_Debug` instance.
  char *filepath;
} LineEntry;

// Returns the line entry for th PC if the line entry contains
// the address of PC. On error `is_ok` is set to false.
LineEntry sd_line_entry_from_pc(Dwarf_Debug dbg, x86_addr pc);

// Get the line entry at the given position.
LineEntry sd_line_entry_at(Dwarf_Debug dbg, const char *filepath,
                           unsigned lineno);

typedef SprayResult (*LineCallback)(LineEntry *line, void *const data);

/* Call `callback` for each new statement line entry
   in the subprogram with the given name. */
SprayResult sd_for_each_line_in_subprog(Dwarf_Debug dbg, const char *fn_name,
                                        const char *filepath,
                                        LineCallback callback,
                                        void *const init_data);

/* Figure out where the function prologue of the function starting
   at `low_pc` ends and return this address. Used for breakpoints on
   functions to break only after the prologue.
   `prologue_start` is the same address as a subprogram's low PC
   and `function_end` is the same address as the high PC. */
SprayResult sd_effective_start_addr(Dwarf_Debug dbg, x86_addr prologue_start,
                                    x86_addr function_end,
                                    x86_addr *function_start);

#ifdef UNIT_TESTS
/* Expose some of the internal interfaces. */

typedef bool (*SearchCallback)(Dwarf_Debug, Dwarf_Die, const void *const, void *const);

int sd_search_dwarf_dbg(
  Dwarf_Debug dbg, Dwarf_Error *const error,
  SearchCallback search_callback,
  const void *const search_for, void *const search_findings
);

bool sd_is_subprog_with_name(Dwarf_Debug dbg, Dwarf_Die die, const char *name);

const char *what_dwarf_result(int dwarf_res);
#endif  // UNIT_TESTS

#endif  // _SPRAY_DWARF_H_
