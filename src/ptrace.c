#include "ptrace.h"

#include <sys/ptrace.h>
#include <assert.h>
#include <errno.h>

enum { PTRACE_ERROR = -1 };

/* NOTE: All `PTRACE_PEEK*` requests return the
 * requested data. Because the return value if
 * always used to indicate an error (by returning
 * -1), `errno` must be used to determine if the
 * result of the read is -1 or there is an error.
 */

SprayResult pt_read_memory(pid_t pid, real_addr addr, uint64_t *read) {
  assert(read != NULL);

  /* The `ptrace(2)` API requires that we manually set `errno` here. */
  errno = 0;
  uint64_t value = ptrace(PTRACE_PEEKDATA, pid, addr, NULL);
  if (errno == 0) {
    /* No error was raised. Return the result. */
    *read = value;
    return SP_OK;
  } else {
    /* `errno` now indicates the error. */
    return SP_ERR;
  }
}

SprayResult pt_write_memory(pid_t pid, real_addr addr, uint64_t write) {
  if (ptrace(PTRACE_POKEDATA, pid, addr, write) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_read_registers(pid_t pid, struct user_regs_struct *regs) {  
  assert(regs != NULL);
  // `addr` is ignored here. `PTRACE_GETREGS` stores all
  // of the tracee's general purpose registers in `regs`.
  if (ptrace(PTRACE_GETREGS, pid, NULL, regs) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_write_registers(pid_t pid, struct user_regs_struct *regs) {
  assert(regs != NULL);
  if (ptrace(PTRACE_SETREGS, pid, NULL, regs) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_continue_execution(pid_t pid) {
  if (ptrace(PTRACE_CONT, pid, NULL, NULL) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_trace_me(void) {
  if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_single_step(pid_t pid) {
  if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}

SprayResult pt_get_signal_info(pid_t pid, siginfo_t *siginfo) {
  assert(siginfo != NULL);
  if (ptrace(PTRACE_GETSIGINFO, pid, NULL, siginfo) == PTRACE_ERROR) {
    return SP_ERR;
  } else {
    return SP_OK;
  }
}
