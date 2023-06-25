#pragma once

#ifndef _SPRAY_DEBUGGER_H_
#define _SPRAY_DEBUGGER_H_

// Required to use `sigabbrev_np`
#define _GNU_SOURCE

#include <stdlib.h>

#include "breakpoints.h"
#include "spray_elf.h"

typedef struct {
  const char *prog_name;
  pid_t pid;
  Breakpoint *breakpoints;
  size_t n_breakpoints;
  ElfFile elf;
  x86_addr load_address;
} Debugger;

// Setup a debugger. This forks the child process.
int setup_debugger(const char *prog_name, Debugger* store);

// Run a debugger.
void run_debugger(Debugger dbg);

#endif  // _SPRAY_DEBUGGER_H_
