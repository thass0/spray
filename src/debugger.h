#pragma once

#ifndef _SPRAY_DEBUGGER_H_
#define _SPRAY_DEBUGGER_H_

/* Required to use `sigabbrev_np` */
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

/* Setup a debugger. This forks the child process, launches
 * and immediately stops it.
 *
 * On success, `dbg` is modified to accommodate the changes.
 *
 On error, `dbg` stays untouched, and `-1` is returned. */
int setup_debugger(const char *prog_name, char *prog_argv[], Debugger *dbg);

/* Run the debugger. Starts debugging at the beginning
 * of the `main` function of the child process.
 *
 * Call `setup_debugger` on `dbg` before calling this function.
 * After `run_debugger` returns, `dbg` is still allocated and
 * must be deleted using `del_debugger`. */
void run_debugger(Debugger dbg);

/* Free memory allocated by the debugger. Returns
 * `SP_ERR` if some resource couldn't be deleted. */
SprayResult del_debugger(Debugger dbg);

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

ExecResult continue_execution(Debugger *dbg);
ExecResult wait_for_signal(Debugger *dbg);

#endif  /* UNIT_TESTS */

#endif  /* _SPRAY_DEBUGGER_H_ */
