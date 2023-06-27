#pragma once

#ifndef _SPRAY_DEBUGGER_H_
#define _SPRAY_DEBUGGER_H_

// Required to use `sigabbrev_np`
#define _GNU_SOURCE

#include <stdlib.h>

#include "breakpoints.h"
#include "spray_elf.h"
#include "spray_dwarf.h"
#include "source_files.h"

typedef struct {
  const char *prog_name;  /* Tracee program name. */
  pid_t pid;  /* Tracee pid. */
  Breakpoint *breakpoints;  /* Breakpoints set in tracee. */
  size_t n_breakpoints;  /* Number of breakpoints. */
  ElfFile elf;  /* Tracee ELF information. */
  x86_addr load_address;  /* Load address. Set for PIEs, 0 otherwise. */
  Dwarf_Debug dwarf;  /* Libdwarf debug information. */
  SourceFiles *files;
} Debugger;

// Setup a debugger. This forks the child process.
int setup_debugger(const char *prog_name, Debugger* store);

// Run a debugger.
void run_debugger(Debugger dbg);

#endif  // _SPRAY_DEBUGGER_H_
