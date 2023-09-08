#pragma once

#ifndef _SPRAY_DEBUGGER_H_
#define _SPRAY_DEBUGGER_H_

// Required to use `sigabbrev_np`
#define _GNU_SOURCE

#include <stdlib.h>

#include "breakpoints.h"
#include "history.h"
#include "info.h"

typedef struct {
  const char *prog_name;     /* Tracee program name. */
  pid_t pid;                 /* Tracee pid. */
  Breakpoints *breakpoints;  /* Breakpoints. */
  DebugInfo *info;           /* Debug information about the tracee. */
  real_addr load_address;    /* Load address. Set for PIEs, 0 otherwise. */
  History history;           /* Command history of recent commands. */
} Debugger;

// Setup a debugger. This forks the child process.
// `store` is only modified on success. The values
// it initially has are never read.
// This launches the debuggee process and immediately stops it.
int setup_debugger(const char *prog_name, char *prog_argv[], Debugger *store);

// Run a debugger. Starts at the beginning of
// the `main` function.
void run_debugger(Debugger dbg);

// Free memory allocated by the debugger.
// Called by `run_debugger`. Returns `SP_ERR`
// if the memory mapped ELF file couldn't be
// unmapped.
SprayResult free_debugger(Debugger dbg);

#endif  // _SPRAY_DEBUGGER_H_
