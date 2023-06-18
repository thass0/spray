#pragma once

#ifndef _SPRAY_DEBUGGER_H_
#define _SPRAY_DEBUGGER_H_

#include <stdlib.h>

#include "breakpoints.h"

typedef struct {
  const char *prog_name;
  pid_t pid;
  Breakpoint *bps;
  size_t nbp;
} Debugger;

// Setup a debugger. This forks the child process.
int setup_debugger(const char *prog_name, Debugger* dest);

// Run a debugger.
void run_debugger(Debugger dbg);

#endif  // _SPRAY_DEBUGGER_H_
