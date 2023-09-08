#pragma once

#ifndef _SPRAY_BREAKPOINTS_H_
#define _SPRAY_BREAKPOINTS_H_

#include <stdlib.h>
#include <stdbool.h>

#include "ptrace.h"

typedef struct Breakpoints Breakpoints;

Breakpoints *init_breakpoints(pid_t pid);

void free_breakpoints(Breakpoints *breakpoints);

/* Enable the given breakpoint by replacing the
   instruction at `addr` with `int 3` (0xcc).

   This will make the child receive a `SIGTRAP` once the
   instruction at address `addr` is reached.

   The tracee's memory stays untouched if an error is returned. */
SprayResult enable_breakpoint(Breakpoints *breakpoints, real_addr addr);

/* Disable a breakpoint, restoring the original instruction.
   Does nothing if there is no breakpoint at `addr`.

   On error, the tracee's memory stays untouched
   and thus the breakpoints remains active. */
SprayResult disable_breakpoint(Breakpoints *breakpoints, real_addr addr);

/* Return `true` if there is a breakpoint at `addr` and
   this breakpoint is enabled. Otherwise, if the breakpoint
   doesn't exist or is disabled, return `false`. */
bool lookup_breakpoint(Breakpoints *breakpoints, real_addr addr);

#endif // _SPRAY_BREAKPOINTS_H_
