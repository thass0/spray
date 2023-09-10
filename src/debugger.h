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


#ifdef UNIT_TESTS

typedef enum {
  EXEC_SIG_EXITED,
  EXEC_SIG_KILLED,
  EXEC_SIG_CONT,
  EXEC_SIG_STOPPED,
  EXEC_NONE,  /* No additionally information. */
} ExecOkCode;

typedef enum {
  EXEC_CONT_DEAD,
  EXEC_INVALID_WAIT_STATUS,
  EXEC_FUNCTION_NOT_FOUND,
  EXEC_SET_BREAKPOINTS_FAILED,
  EXEC_PC_LINE_NOT_FOUND,
  EXEC_STEP,
} ExecErrCode;

typedef struct {
  SprayResult type;
  union {
    ExecOkCode ok;
    ExecErrCode err;
  } code;
  union {
    struct {
      int signo;
      int code;  /* `si_code` field of `siginfo_t` struct. */
    } signal;  /* Set for `EXEC_KILLED` and `EXEC_STOPPED`. */
    int exit_code;  /* Set for `EXEC_EXITED`. */
    int wait_status;  /* Set for `EXEC_INVALID_WAIT_STATUS`. */
  } data;
} ExecResult;

ExecResult continue_execution(Debugger dbg);
ExecResult wait_for_signal(Debugger dbg);

#endif  // UNIT_TESTS

#endif  // _SPRAY_DEBUGGER_H_
