#include "debugger.h"
#include "magic.h"
#include "registers.h"
#include "ptrace.h"

#include "../dependencies/linenoise/linenoise.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/personality.h>


// ==============
// Error Messages
// ==============

#define HEX_FORMAT "0x%016lx"

static inline void print_addr(x86_addr addr) {
  printf(HEX_FORMAT, addr.value);
}

static inline void print_word(x86_word word) {
  printf(HEX_FORMAT, word.value);
}

/* NOTE: Command error messages are stored
 * in the following enum and array because
 * most of them (all except for one) are
 * used twice. This ensures they are always
 * spelled the same. */

typedef enum {
  BREAK_LOC,
  DELETE_LOC,
  REGISTER_NAME,
  REGISTER_OPERATION,
  REGISTER_WRITE_VALUE,
  MEMORY_ADDR,
  MEMORY_OPERATION,
  MEMORY_WRITE_VALUE,
  MEMORY_READ_ACTION,
  MEMORY_WRITE_ACTION,
  MEMORY_CONFIRM_READ_ACTION,
  REGISTER_READ_ACTION,
  REGISTER_WRITE_ACTION,
  REGISTER_CONFIRM_READ_ACTION,
  REGISTER_PRINT_ACTION,
} ErrorCode;

static const char* error_messages[] = {
  [BREAK_LOC]="location for break",
  [DELETE_LOC]="location for delete",
  [REGISTER_NAME]="register name for register operation",
  [REGISTER_OPERATION]="register operation",
  [REGISTER_WRITE_VALUE]="value for register write",
  [MEMORY_ADDR]="address for memory operation",
  [MEMORY_OPERATION]="memory operation",
  [MEMORY_WRITE_VALUE]="value for memory write",
  [MEMORY_READ_ACTION]="read from tracee memory",
  [MEMORY_WRITE_ACTION]="write to tracee memory",
  [MEMORY_CONFIRM_READ_ACTION]="read from tracee memory to confirm successful write",
  [REGISTER_READ_ACTION]="read from tracee register",
  [REGISTER_WRITE_ACTION]="write to tracee register",
  [REGISTER_CONFIRM_READ_ACTION]="read from tracee register to confirm successful write",
  [REGISTER_PRINT_ACTION]="read all tracee registers for register dump",
};

static inline void invalid_error(ErrorCode what) {
  printf("🤦 Invalid %s\n", error_messages[what]);
}

static inline void missing_error(ErrorCode what) {
  printf("🤦 Missing %s\n", error_messages[what]);
}

static inline void internal_error(const char *format, ...) {
  va_list argp;
  va_start(argp, format);
  printf("💢 ");
  vprintf(format, argp);
  printf("\n");
  va_end(argp);
}

static inline void internal_memory_error(ErrorCode what, x86_addr addr) {
  printf("💢 Failed to %s (address ", error_messages[what]);
  print_addr(addr);
  printf(")\n");
}

static inline void internal_register_error(ErrorCode what, x86_reg reg) {
  printf("💢 Failed to %s (register '%s')\n",
    error_messages[what], get_name_from_register(reg));
}

static inline void empty_command_error(void) {
  printf("🤔 No command to repeat\n");
}

static inline void unknown_cmd_error(void) {
  printf("🤔 I don't know that\n");
}

static inline void command_unfinished_error(void) {
  printf("🤦 Trailing characters in command\n");
}

static inline void missing_source_error(x86_addr addr) {
  printf("<No source info for PC ");
  print_addr(addr);
  printf(">\n");
}


// ========================
// PC and Address Utilities
// ========================

/* NOTE: Breakpoints use *read addresses*. */

/* Remove offset of position independet executables
   from the given address to make it work with DWARF. */
x86_addr real_to_dwarf(Debugger dbg, x86_addr real) {
  return (x86_addr) { real.value - dbg.load_address.value };
}

/* Add the offset of position independent executables
   to the given address. This turns an address from DWARF
   info a real address. */
x86_addr dwarf_to_real(x86_addr load_address, x86_addr dwarf) {
  return (x86_addr) { dwarf.value + load_address.value };
}

/* Get the program counter. */
x86_addr get_pc(pid_t pid) {
  x86_addr store = { 0 };
  SprayResult res = get_register_value(pid, rip, (x86_word *) &store);
  if (res == SP_OK) {
    return store;
  } else {
    return (x86_addr) { 0 };
  }
}

/* Get the program counter and remove any offset which is
   added to the physical process counter in PIEs. The DWARF
   debug info doesn't know about this offset. */
x86_addr get_dwarf_pc(Debugger dbg) {
  x86_addr real_pc = get_pc(dbg.pid);
  return real_to_dwarf(dbg, real_pc);
}

/* Set the program counter. */
void set_pc(pid_t pid, x86_addr pc) {
  set_register_value(pid, rip, (x86_word) { pc.value });
}


// =====================================
// Command and Internal Execution Result
// =====================================

void print_current_source(Debugger dbg) {
  x86_addr pc = get_dwarf_pc(dbg);
  LineEntry line_entry = get_line_entry_from_pc(dbg.dwarf, pc);
  if (line_entry.is_ok) {
    SprayResult res = print_source(
      dbg.files,
      line_entry.filepath,
      line_entry.ln,
      3);
    if (res == SP_ERR) {
      internal_error("Failed to read source file %s; "
        "can't print source", line_entry.filepath);
    }
  } else {
    missing_source_error(pc);
  }
}

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
  EXEC_FILEPATH_NOT_FOUND,
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

/* The following function construct `ExecResult`s
   which are valid for the given codes. Don't
   construct `ExecResult`s by hand. */

static inline ExecResult exec_ok(void) {
  return (ExecResult) {
    .type=SP_OK,
    .code.ok=EXEC_NONE,
  };
}
static inline ExecResult exec_sig_exited(int exit_code) {
  return (ExecResult) {
    .type=SP_OK,
    .code.ok=EXEC_SIG_EXITED,
    .data.exit_code=exit_code,
  };
}
static inline ExecResult exec_sig_killed(int signo) {
  return (ExecResult) {
    .type=SP_OK,
    .code.ok=EXEC_SIG_KILLED,
    .data.signal.signo=signo,
  };
}
static inline ExecResult exec_sig_cont(void) {
  return (ExecResult) {
    .type=SP_OK,
    .code.ok=EXEC_SIG_CONT,
  };
}
static inline ExecResult exec_sig_stopped(int signo, int si_code) {
  return (ExecResult) {
    .type=SP_OK,
    .code.ok=EXEC_SIG_STOPPED,
    .data.signal={
      .signo=signo,
      .code=si_code,
    },
  };
}
static inline ExecResult exec_continue_dead(void) {
  return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_CONT_DEAD,
  };
}
static inline ExecResult exec_invalid_wait_status(int wait_status) {
  return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_INVALID_WAIT_STATUS,
    .data.wait_status=wait_status,
  };
}
static inline ExecResult exec_function_not_found(void) {
  return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_FUNCTION_NOT_FOUND,
  };
}
static inline ExecResult exec_filepath_not_found(void) {
  return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_FILEPATH_NOT_FOUND,
  };
}
static inline ExecResult exec_pc_line_not_found(void) {
  return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_PC_LINE_NOT_FOUND,
  };
}
static  inline ExecResult exec_step_target_not_found(void) {
    return (ExecResult) {
    .type=SP_ERR,
    .code.err=EXEC_STEP,
  };
}

static inline bool is_exec_err(ExecResult *exec_result) {
  if (exec_result  == NULL) {
    return true;
  } else {
    return exec_result->type == SP_ERR;  
  }
}
/* static inline bool is_exec_ok(ExecResult *exec_result) {
  if (exec_result == NULL) {
    return false;
  } else {
    return exec_result->type == SP_OK;
  }
} */

void print_exec_ok(Debugger dbg, ExecResult *exec_res) {
  assert(exec_res != NULL);
  switch (exec_res->code.ok) {
    case EXEC_NONE:
      break;
    case EXEC_SIG_EXITED:
      printf("Child exited with code %d\n",
        exec_res->data.exit_code);
      break;
    case EXEC_SIG_KILLED:
      printf("Child was terminated by signal SIG%s\n",
        sigabbrev_np(exec_res->data.signal.signo));
      break;
    case EXEC_SIG_STOPPED: {
      int signo = exec_res->data.signal.signo;
      int code = exec_res->data.signal.code;
      if (signo == SIGSEGV) {
        printf("Child was stopped by a segmentation "
          "fault, reason %d\n", code);
      } else if (signo == SIGTRAP) {
        if (code == SI_KERNEL || code == TRAP_BRKPT) {
          if (lookup_breakpoint(dbg.breakpoints, get_pc(dbg.pid))) {
            /* Only print 'Hit breakpoint' message if the breakpoint that
               causes this SIGTRAP is permanent, i.e. set by the user. */
            printf("Hit breakpoint at address ");
            print_addr(get_pc(dbg.pid));
            printf("\n");
          }
          print_current_source(dbg);
        } else {
          printf("Child was stopped by signal SIGTRAP\n");
        }
      } else {
        printf("Child was stopped by SIG%s\n",
          sigabbrev_np(signo));
      }
      break;
    }
    case EXEC_SIG_CONT:
      printf("Child was resumed\n");
      break;
  }
}

void print_exec_err(ExecResult *exec_res) {
  assert(exec_res != NULL);
  switch (exec_res->code.err) {
    case EXEC_CONT_DEAD:
      printf("💀 The process is dead\n");
      break;
    case EXEC_INVALID_WAIT_STATUS:
      printf("Internal error: received invalid wait status %d",
        exec_res->data.wait_status);
      break;
    case EXEC_FUNCTION_NOT_FOUND:
      internal_error("Failed to find current function");
      break;
    case EXEC_FILEPATH_NOT_FOUND:
      internal_error("Failed to find path to current file");
      break;
    case EXEC_PC_LINE_NOT_FOUND:
      internal_error("Failed to find current line");
      break;
    case EXEC_STEP:
      internal_error("Failed to find another line to step to");
      break;
  }
}

void print_exec_res(Debugger dbg, ExecResult exec_res) {
  if (exec_res.type == SP_OK) {
    print_exec_ok(dbg, &exec_res);
  } else {
    print_exec_err(&exec_res);
  }
}


// =============================
// Stepping and Breakpoint Logic
// =============================

ExecResult wait_for_signal(Debugger dbg);

/* Execute the instruction at the breakpoints location
   and stop the tracee again. */
ExecResult single_step_breakpoint(Debugger dbg) {
  x86_addr pc_address = get_pc(dbg.pid);

  if (lookup_breakpoint(dbg.breakpoints, pc_address)) {
    /* Disable the breakpoint, run the original
       instruction and stop. */
    disable_breakpoint(dbg.breakpoints, pc_address);
    pt_single_step(dbg.pid);
    ExecResult exec_res = wait_for_signal(dbg);
    enable_breakpoint(dbg.breakpoints, pc_address);
    return exec_res;
  } else {
    return exec_ok();
  }
}

/* Continue execution of child process. If the PC is currently
   hung up on a breakpoint then that breakpoint is stepped-over. */
ExecResult continue_execution(Debugger dbg) {
  single_step_breakpoint(dbg);

  errno = 0;
  pt_continue_execution(dbg.pid);
  /* Is the process still alive? */
  if (errno == ESRCH) {
    return exec_continue_dead();
  }

  return exec_ok();
}

void handle_sigtrap(Debugger dbg, siginfo_t siginfo) {
  /* Did the tracee hit a breakpoint? */
  if (
    siginfo.si_code == SI_KERNEL ||
    siginfo.si_code == TRAP_BRKPT
  ) {
    /* Go back to real breakpoint address. */
    x86_addr pc = get_pc(dbg.pid);
    set_pc(dbg.pid, (x86_addr) {pc.value - 1});
  }
}

ExecResult wait_for_signal(Debugger dbg) {
  /* Wait for the tracee to be stopped by receiving a
   * signal. Once the tracee is stopped again we can
   * continue to poke at it. Effectively this is just
   * waiting until the next breakpoint sends the tracee
   * another SIGTRAP. */
  /* In general the following events are awaited by `waitpid`:
   * - child is terminated
   * - child is stopped by a signal
   * - child resumes from a signal
   */
  int wait_status;  /* Store status info here. */
  int options = 0;  /* Normal behviour. */
  waitpid(dbg.pid, &wait_status, options);

  /* Display some info about the state-change which
   * has just stopped the tracee. This helps grasp
   * what state the tracee is in now that we can
   * inspect it. */

  // Did the tracee terminate normally?
  if (WIFEXITED(wait_status)) {
    return exec_sig_exited(WEXITSTATUS(wait_status));
  }
  // Was the tracee terminated by a signal?
  else if (WIFSIGNALED(wait_status)) {
    return exec_sig_killed(WTERMSIG(wait_status));
  }
  // Did the tracee receive a `SIGCONT` signal?
  else if (WIFCONTINUED(wait_status)) {
    return exec_sig_cont();
  }
  // Was the tracee stopped by another signal?
  else if (WIFSTOPPED(wait_status)) {
    siginfo_t siginfo = { 0 };
    pt_get_signal_info(dbg.pid, &siginfo);

    switch (siginfo.si_signo) {
      case SIGSEGV: {
        return exec_sig_stopped(SIGSEGV, siginfo.si_code);
      }
      case SIGTRAP: {
        handle_sigtrap(dbg, siginfo);
        return exec_sig_stopped(SIGTRAP, siginfo.si_code);
      }
    case SIGWINCH: {
        /* Ignore changes in window size by telling the
           tracee to continue in that case and then wait for
           the next interesting signal. */
        continue_execution(dbg);
        return wait_for_signal(dbg);
      }
      default: {
        return exec_sig_stopped(WSTOPSIG(wait_status), 0);
      }
    }
  } else {
    return exec_invalid_wait_status(wait_status);
  }
}

ExecResult single_step_instruction(Debugger dbg) {
  if (lookup_breakpoint(dbg.breakpoints, get_pc(dbg.pid))) {
    single_step_breakpoint(dbg);
    return exec_ok();
  } else {
    pt_single_step(dbg.pid);
    return wait_for_signal(dbg);
  }
}

/* Set a breakpoint on the current return address.
   Used for source-level stepping. Returns whether or
   not the breakpoint must be removed again after use.
   If it was to be removed,  the value of `return_address`
   is set to the address where the breakpoint was created. */
bool set_return_address_breakpoint(Breakpoints *breakpoints, pid_t pid, x86_addr *return_address) {
  assert(breakpoints != NULL);
  assert(return_address != NULL);

  /* The return address is stored 8 bytes after the
     start of the stack frame. This is where we want
     to set a breakpoint. */
  x86_addr frame_pointer = { 0 };
  SprayResult res = get_register_value(pid, rbp, (x86_word *) &frame_pointer);
  assert(res == SP_OK);
  x86_addr return_address_location = { frame_pointer.value + 8 };
  pt_read_memory(pid, return_address_location, (x86_word *) return_address);

  bool remove_transient_breakpoint = false;
  if (!lookup_breakpoint(breakpoints, *return_address)) {
    enable_breakpoint(breakpoints, *return_address);
    remove_transient_breakpoint = true;
  }

  return remove_transient_breakpoint;
}

/* Step outside of the current function. */
ExecResult step_out(Debugger dbg) {
  x86_addr return_address = { 0 };
  bool remove_internal_breakpoint = set_return_address_breakpoint(
    dbg.breakpoints,
    dbg.pid,
    &return_address);

  continue_execution(dbg);
  ExecResult exec_res = wait_for_signal(dbg);

  if (remove_internal_breakpoint) {
    delete_breakpoint(dbg.breakpoints, return_address);
  }

  return exec_res;
}

/* Single step instructions until the line number has changed. */
ExecResult single_step_line(Debugger dbg) {
  unsigned init_lineno = get_line_entry_from_pc(dbg.dwarf, get_dwarf_pc(dbg)).ln;

  unsigned n_instruction_steps = 0;
  LineEntry next_line = get_line_entry_from_pc_exact(dbg.dwarf, get_dwarf_pc(dbg));
  /* Single step instructions until we find a valid line
     with a different line number than before. */
  while (!next_line.is_ok || next_line.ln == init_lineno) {
    ExecResult exec_res = single_step_instruction(dbg);
    if (is_exec_err(&exec_res)) {
      return exec_res;
    }

    next_line = get_line_entry_from_pc_exact(dbg.dwarf, get_dwarf_pc(dbg));
    n_instruction_steps ++;

    /* Did we reach the maximum number of steps? */
    if (n_instruction_steps >= SINGLE_STEP_SEARCH_LIMIT) {
      return exec_step_target_not_found();
    }
  }

  return exec_ok();
}

typedef struct {
  size_t to_del_alloc;
  size_t to_del_idx;
  x86_addr *to_del_breakpoints;
  x86_addr load_address;
  unsigned orig_lineno;
  Breakpoints *breakpoints;
} CallbackData;

enum { TO_DEL_ALLOC_SIZE=8 };

SprayResult callback__set_dwarf_line_breakpoint(LineEntry *line, void *const void_data) {
  assert(line != NULL);
  assert(void_data != NULL);

  CallbackData *data = (CallbackData *) void_data;

  x86_addr real_line_addr = dwarf_to_real(data->load_address,
                                          line->addr);

  if (
    data->orig_lineno != line->ln &&
    !lookup_breakpoint(data->breakpoints, real_line_addr)
  ) {
    enable_breakpoint(data->breakpoints, real_line_addr);

    if (data->to_del_idx >= data->to_del_alloc) {
      data->to_del_alloc += TO_DEL_ALLOC_SIZE;
      data->to_del_breakpoints = (x86_addr *)
        realloc (data->to_del_breakpoints, sizeof(x86_addr) * data->to_del_alloc);
      assert(data->to_del_breakpoints != NULL);
    }
    data->to_del_breakpoints[data->to_del_idx++] = real_line_addr;
  }

  return SP_OK;
}

/* Step to the next line. Don't step into functions. */
ExecResult step_over(Debugger dbg) {
  /* This functions sets breakpoints all over the current DWARf
     subprogram so that we step no matter what the line we step
     over does. */

  LineEntry current_line_entry = get_line_entry_from_pc(dbg.dwarf, get_dwarf_pc(dbg));
  if (!current_line_entry.is_ok) {
    return exec_pc_line_not_found();
  }

  unsigned current_lineno = current_line_entry.ln;

  x86_addr pc = get_dwarf_pc(dbg);
  char *fn_name = get_function_from_pc(dbg.dwarf, pc);
  if (fn_name == NULL) {
    return exec_function_not_found();
  }
  char *filepath = get_filepath_from_pc(dbg.dwarf, pc);
  if (filepath == NULL) {
    free(fn_name);
    return exec_filepath_not_found();
  }

  size_t to_del_alloc = TO_DEL_ALLOC_SIZE;
  size_t to_del_idx = 0;
  x86_addr *to_del_breakpoints =
    (x86_addr *) calloc (TO_DEL_ALLOC_SIZE, sizeof(x86_addr));
  assert(to_del_breakpoints != NULL);

  CallbackData data = {
    to_del_alloc,
    to_del_idx,
    to_del_breakpoints,
    dbg.load_address,
    current_lineno,
    dbg.breakpoints,
  };

  for_each_line_in_subprog(
    dbg.dwarf,
    fn_name,
    filepath,
    callback__set_dwarf_line_breakpoint,
    &data
  );

  free(fn_name);
  free(filepath);

  x86_addr return_address = { 0 };
  bool remove_internal_breakpoint = set_return_address_breakpoint(
    dbg.breakpoints,
    dbg.pid,
    &return_address
  );

  continue_execution(dbg);
  ExecResult exec_res = wait_for_signal(dbg);

  for (size_t i = 0; i < data.to_del_idx; i++) {
    delete_breakpoint(
      dbg.breakpoints,
      data.to_del_breakpoints[i]);
  }
  free(data.to_del_breakpoints);

  if (remove_internal_breakpoint) {
    delete_breakpoint(
      dbg.breakpoints,
      return_address
    );
  }

  return exec_res;
}


// =================
// Command Execution
// =================

/* This amount of indentation ensures that the
   contents of memory reads are indented the
   same amount as register reads which are
   preceeded by a register name. */
#define MEM_READ_INDENT "         "

/* Message displayed right after the new value
   of a memory location or register written to
   was displayed. This is useful as confirmation
   that the write operation was successful. */
#define WRITE_READ_MSG "(read after write)"

void exec_command_memory_read(pid_t pid, x86_addr addr) {
  x86_word read = { 0 };
  SprayResult res = pt_read_memory(pid, addr, &read);
  if (res == SP_OK) {
    printf(MEM_READ_INDENT);
    print_word(read);
    printf("\n");
  } else {
    internal_memory_error(MEMORY_READ_ACTION, addr);
  }
}

void exec_command_memory_write(pid_t pid, x86_addr addr, x86_word word) {
  SprayResult write_res = pt_write_memory(pid, addr, word);
  if (write_res == SP_ERR) {
    internal_memory_error(MEMORY_WRITE_ACTION, addr);
    return;
  }

  /* Print readout of write result: */
  x86_word stored = { 0 };
  SprayResult read_res = pt_read_memory(pid, addr, &stored);
  if (read_res == SP_OK) {
    printf(MEM_READ_INDENT);
    print_word(stored);
    printf(" %s\n", WRITE_READ_MSG);
  } else {
    internal_memory_error(MEMORY_CONFIRM_READ_ACTION, addr);
  }
}

void exec_command_register_read(pid_t pid, x86_reg reg, const char *restrict reg_name) {
  x86_word reg_word = { 0 };
  SprayResult res = get_register_value(pid, reg, &reg_word);
  if (res == SP_OK) {
    printf("%8s ", reg_name);
    print_word(reg_word);
    printf("\n");
  } else {
    internal_register_error(REGISTER_READ_ACTION, reg);
  }
}

void exec_command_register_write(
  pid_t pid,
  x86_reg reg,
  const char *restrict reg_name,
  x86_word word
) {
  SprayResult write_res = set_register_value(pid, reg, word);
  if (write_res == SP_ERR) {
    internal_register_error(REGISTER_WRITE_ACTION, reg);
  }

  /* Print readout of write result: */
  x86_word written = { 0 };
  SprayResult read_res = get_register_value(pid, reg, &written);
  if (read_res == SP_OK) {
    printf("%8s ", reg_name);
    print_word(written);
    printf(" %s\n", WRITE_READ_MSG);
  } else {
    internal_register_error(REGISTER_CONFIRM_READ_ACTION, reg);
  }
}

void exec_command_print(pid_t pid) {
  char *buf = (char *) calloc (REGISTER_PRINT_BUF_SIZE, sizeof(char));
  assert(buf != NULL);
  size_t pos = 0;

  for (size_t i = 0; i < N_REGISTERS; i++) {
    reg_descriptor desc = reg_descriptors[i];

    x86_word reg_word = { 0 };
    SprayResult res = get_register_value(pid, desc.r, &reg_word);
    if (res == SP_ERR) {
      free(buf);
      internal_register_error(REGISTER_PRINT_ACTION, desc.r);
      return;
    }

    snprintf(
      buf + pos,
      REGISTER_PRINT_LEN + 1,
      "\t%8s " HEX_FORMAT,
      desc.name,
      reg_word.value
    );
    pos += REGISTER_PRINT_LEN;

    // Always put two registers on the same line.
    if (i % 2 == 1) {
      buf[pos ++] = '\n';
    }
  }

  assert(pos == REGISTER_PRINT_BUF_SIZE - 1);
  buf[pos] = '\0';

  puts(buf);
  free(buf);
}

void exec_command_break(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);
  enable_breakpoint(breakpoints, addr);
}

void exec_command_delete(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);
  disable_breakpoint(breakpoints, addr);
}

/* Execute the instruction at the current breakpoint,
   continue the tracee and wait until it receives the
   next signal. */
void exec_command_continue(Debugger dbg) {
  ExecResult cont_res = continue_execution(dbg);
  if (is_exec_err(&cont_res)) {
    print_exec_res(dbg, cont_res);
  } else {
    ExecResult exec_res = wait_for_signal(dbg);
    print_exec_res(dbg, exec_res);
  }
}

void exec_command_single_step_instruction(Debugger dbg) {
  ExecResult exec_res = single_step_instruction(dbg);
  if (is_exec_err(&exec_res)) {
    print_exec_res(dbg, exec_res);
  } else {
    print_current_source(dbg);
  }
}

void exec_command_step_out(Debugger dbg) {
  ExecResult exec_res = step_out(dbg);
  print_exec_res(dbg, exec_res);
}

void exec_command_single_step(Debugger dbg) {
  /* Single step instructions until the line number
  has changed. */
  ExecResult exec_res = single_step_line(dbg);
  if (is_exec_err(&exec_res)) {
    print_exec_res(dbg, exec_res);
  } else {
    print_current_source(dbg);
  }
}

void exec_command_step_over(Debugger dbg) {
  ExecResult exec_res = step_over(dbg);
  print_exec_res(dbg, exec_res);
}


// ===============
// Command Parsing
// ===============

static inline const char *get_next_token(char *const *tokens, size_t *i) {
  assert(i != NULL);
  const char *ret = tokens[*i];
  if (ret  == NULL) {
    return NULL;
  } else {
    *i += 1;
    return ret;
  }
}

static inline bool line_is_parsed(char *const *tokens, size_t i) {
  if (tokens[i] == NULL) {
    return true;
  } else {
    command_unfinished_error();
    return false;
  }
}

bool is_command(
  const char *restrict in,
  const char *restrict short_from,
  const char *restrict long_form
) {
  if (in != NULL) {
    return (strcmp(in, short_from) == 0)
      || (strcmp(in, long_form) == 0);
  } else {
    return false;
  }
}

SprayResult parse_base16(const char *restrict str, uint64_t *store) {
  char *str_end;
  uint64_t value = strtol(str, &str_end, 16);
  if (str[0] != '\0' && *str_end == '\0') {
    *store = value;
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

SprayResult check_function_name(const char *func_name) {
  /* Regular expression for identifiers from the 2011 ISO C
     standard grammar (https://www.quut.com/c/ANSI-C-grammar-l-2011.html):
       L   [a-zA-Z_]
       A   [a-zA-Z_0-9]
       {L}{A}*
  */
  regex_t ident_regex;
  int comp_res = regcomp(&ident_regex,
                         "^[a-zA-Z_][a-zA-Z_0-9]*$",
                         REG_NOSUB | REG_EXTENDED);
  /* The regex doesn't change so compilation shouldn't fail. */
  assert(comp_res == 0);
  int match = regexec(&ident_regex,
                      func_name,
                      0,     // We are not interested
                      NULL,  // in any subexpressions.
                      0);
  regfree(&ident_regex);
  if (match == 0) {
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

SprayResult check_file_line(const char *file_line) {
  regex_t file_line_regex;
  int comp_res = regcomp(&file_line_regex,
                         "^[^:]+:[0-9]+$",
                         REG_EXTENDED);
  assert(comp_res == 0);
  
  int match = regexec(&file_line_regex, file_line, 0, NULL, 0);
  regfree(&file_line_regex);
  if (match == 0) {
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

SprayResult parse_lineno(const char *line_str, unsigned *line_dest) {
  char *str_end = 0;
  long line_buf = strtol(line_str, &str_end, 10);
  if (
    line_str[0] != '\0' && *str_end == '\0' &&
    line_buf <= UINT_MAX
  ) {
    *line_dest = line_buf;
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

SprayResult parse_break_location(Debugger dbg,
                                 const char *location,
                                 x86_addr *dest
) {
  if (check_function_name(location) == SP_OK ){
    SprayResult res = get_function_start_addr(dbg.dwarf,
                                              location,
                                              dest);
    return res;
  } else if (parse_base16(location, &dest->value) == SP_OK) {
    return SP_OK;
  } else if (check_file_line(location) == SP_OK) {
    char *location_cpy = strdup(location);
    const char *filepath = strtok(location_cpy, ":");
    assert(filepath != NULL);  // OK since `location` was validated.

    unsigned lineno = 0;
    SprayResult res = parse_lineno(strtok(NULL, ":"), &lineno);
    if (res == SP_ERR) {
      free(location_cpy);
      return SP_ERR;
    }

    LineEntry line = get_line_entry_at(dbg.dwarf, filepath, lineno);
    free(location_cpy);
    if (line.is_ok) {
      *dest = line.addr;
      return SP_OK;
    } else {
      return SP_ERR;
    }
  } else {
    return SP_ERR;
  }
}

char **get_command_tokens(const char *line) {
  enum { TOKENS_ALLOC=8 };
  size_t n_alloc = TOKENS_ALLOC;
  char **tokens = (char **) calloc (n_alloc, sizeof(char *));
  assert(tokens != NULL);

  char *line_buf = strdup(line);
  assert(line_buf != NULL);

  const char *token = strtok(line_buf, " \t");
  size_t i = 0;

  while (token != NULL) {
    if (i >= n_alloc) {
      n_alloc += TOKENS_ALLOC;
      tokens = (char **) realloc (tokens,
                                        n_alloc * sizeof(char*));
      assert(tokens != NULL);
    }
    char *token_cpy = strdup(token);
    tokens[i] = token_cpy;
    token = strtok(NULL, " \t");
    i ++;
  }

  if (i >= n_alloc) {
    n_alloc += 1;
    tokens = (char **) realloc (tokens,
                                      n_alloc * sizeof(char*));
    assert(tokens != NULL);
  }

  tokens[i] = NULL;
  free(line_buf);

  return tokens;
}

void free_command_tokens(char **tokens) {
  if (tokens != NULL) {
    for (size_t i = 0; tokens[i] != NULL; i++) {
      free(tokens[i]);
    }
    free(tokens);
  }
}

void handle_debug_command_tokens(Debugger* dbg, char *const *tokens) {
  assert(dbg != NULL);
  assert(tokens != NULL);

  size_t i = 0;
  const char *cmd = get_next_token(tokens, &i);

  do {
    if (is_command(cmd, "c", "continue")) {
      if (!line_is_parsed(tokens, i)) break;
      exec_command_continue(*dbg);
    } else if (is_command(cmd, "b", "break")) {
      // Pass `NULL` to `strtok_r` to continue scanning `line`.
      const char *loc_str = get_next_token(tokens, &i);
      if (loc_str == NULL) {
        missing_error(BREAK_LOC);
      } else {
        x86_addr addr = { 0 };
        if (parse_break_location(*dbg, loc_str, &addr) == SP_OK) {
          if (!line_is_parsed(tokens, i))
            break;
          exec_command_break(dbg->breakpoints, addr);
        } else {
          invalid_error(BREAK_LOC);
        }
      }
    } else if (is_command(cmd, "d", "delete")) {
      const char *loc_str = get_next_token(tokens, &i);
      if (loc_str == NULL) {
        missing_error(DELETE_LOC);
      } else {
        x86_addr addr = { 0 };
        if (parse_break_location(*dbg, loc_str, &addr) == SP_OK) {
          if (!line_is_parsed(tokens, i))
            break;
          exec_command_delete(dbg->breakpoints, addr);
        } else {
          invalid_error(DELETE_LOC);
        }
      }
    } else if (is_command(cmd, "r", "register")) {
      const char *name = get_next_token(tokens, &i);
      x86_reg reg;
      if (name == NULL) {
        missing_error(REGISTER_NAME);
        break;
      } else if (is_command(name, "dump", "print")) {
        if (!line_is_parsed(tokens, i)) break;
        /* This is an exception: instead of a name the register
         * operation could also be followed by a `dump`/`print` command.
         */
        exec_command_print(dbg->pid);
        break;
      } else {
        /* Read the register of interest. */
        x86_reg reg_buf;
        bool found_register = get_register_from_name(name, &reg_buf);
        if (found_register) {
          reg = reg_buf;
        } else {
          invalid_error(REGISTER_NAME);
          break;
        }
      }

      const char *op_str = get_next_token(tokens, &i);
      if (op_str == NULL) {
        missing_error(REGISTER_OPERATION);
      } else {
        if (is_command(op_str, "rd", "read")) {
          if (!line_is_parsed(tokens, i)) break;
          /* Read */
          exec_command_register_read(dbg->pid, reg, name);
        } else if (is_command(op_str, "wr", "write")) {
          /* Write */
          const char *value_str = get_next_token(tokens, &i);
          if (value_str == NULL) {
            missing_error(REGISTER_WRITE_VALUE);
          } else {
            x86_word word;
            if (parse_base16(value_str, &word.value) == SP_OK) {
              if (!line_is_parsed(tokens, i)) break;
              exec_command_register_write(dbg->pid, reg, name, word);
            } else {
              invalid_error(REGISTER_WRITE_VALUE);
            }
          }
        } else {
          invalid_error(REGISTER_OPERATION);
        }
      }
    } else if (is_command(cmd, "m", "memory")) {
      const char *addr_str = get_next_token(tokens, &i);
      x86_addr addr;
      if (addr_str == NULL) {
        missing_error(MEMORY_ADDR);
        break;
      } else {
        x86_addr addr_buf;
        if (parse_base16(addr_str, &addr_buf.value) == SP_OK) {
          addr = addr_buf;
        } else {
          invalid_error(MEMORY_ADDR);
          break;
        }
      }

      const char *op_str = get_next_token(tokens, &i);
      if (op_str == NULL) {
        missing_error(MEMORY_OPERATION);
      } else if (is_command(op_str, "rd", "read")) {
        if (!line_is_parsed(tokens, i)) break;
        /* Read */
        exec_command_memory_read(dbg->pid, addr);
      } else if (is_command(op_str, "wr", "write")) {
        const char *value_str = get_next_token(tokens, &i);
        if (value_str == NULL) {
          missing_error(MEMORY_WRITE_VALUE);
        } else {
          x86_word word;
          if (parse_base16(value_str, &word.value) == SP_OK) {
            if (!line_is_parsed(tokens, i)) break;
            exec_command_memory_write(dbg->pid, addr, word);
          } else {
            invalid_error(MEMORY_WRITE_VALUE);
          }
        }
      } else {
        invalid_error(MEMORY_OPERATION);
      }
    } else if (is_command(cmd, "i", "inst")) {
      if (!line_is_parsed(tokens, i)) break;
      exec_command_single_step_instruction(*dbg);
    } else if (is_command(cmd, "l", "leave")) {
      if (!line_is_parsed(tokens, i)) break;
      exec_command_step_out(*dbg);
    } else if (is_command(cmd, "s", "step")) {
      if (!line_is_parsed(tokens, i)) break;
      exec_command_single_step(*dbg);
    } else if (is_command(cmd, "n", "next")) {
      if (!line_is_parsed(tokens, i)) break;
      exec_command_step_over(*dbg);
    } else {
      unknown_cmd_error();
    }
  } while (0); /* Only run this block once. The
   * loop is only used to make `break` available
   * for  skipping subsequent steps on error. */
}

void handle_debug_command(Debugger *dbg, const char *line) {
  assert(dbg != NULL);
  assert(line != NULL);

  char **tokens = get_command_tokens(line);
  size_t i = 0;
  const char *first_token = get_next_token(tokens, &i);

  /* Is the command empty? */
  if (first_token == NULL) {
    free_command_tokens(tokens);
    char *last_command = NULL;
    SprayResult res = read_command(dbg->history, &last_command);
    if (res == SP_OK) {
      tokens = get_command_tokens(last_command);
      free(last_command);
    } else {
      empty_command_error();
      return;
    }
  } else {
    save_command(dbg->history, line);
  }

  handle_debug_command_tokens(dbg, tokens);
  free_command_tokens(tokens);
}


// =======================
// Debugger Initialization
// =======================

void init_load_address(Debugger *dbg) {
  assert(dbg != NULL);

  // Is this a dynamic executable?
  if (dbg->elf.type == ELF_TYPE_DYN) {
    // Open the process' `/proc/<pid>/maps` file.
    char proc_maps_filepath[PROC_MAPS_FILEPATH_LEN];
    snprintf(
      proc_maps_filepath,
      PROC_MAPS_FILEPATH_LEN,
      "/proc/%d/maps",
      dbg->pid);

    FILE *proc_map = fopen(proc_maps_filepath, "r");
    assert(proc_map != NULL);

    // Read the first address from the file.
    // This is OK since address space
    //  layout randomization is disabled.
    char *addr = NULL;
    size_t n = 0;
    ssize_t nread = getdelim(&addr, &n, (int) '-', proc_map);
    fclose(proc_map);
    assert(nread != -1);

    x86_addr load_address = { 0 };
    assert(parse_base16(addr, &load_address.value) == SP_OK);

    free(addr);

    // Now update the debugger instance on success.
    dbg->load_address = load_address;
  } else {
    dbg->load_address = (x86_addr) { 0 };
  }
}

int setup_debugger(const char *prog_name, char *prog_argv[], Debugger* store) {
  assert(store != NULL);

  if (access(prog_name, F_OK) != 0) {
    fprintf(stderr, "File %s doesn't exist\n", prog_name);
    return -1;
  }

  // Parse the ELF header.
  ElfFile elf_buf;  /* Must buffer currently because `parse_elf`
                       might change `elf_buf` even on error.
                       This function however should only modify
                       `store` if it's successful. */
  ElfParseResult res = parse_elf(prog_name, &elf_buf);
  if (res != ELF_PARSE_OK) {
    fprintf(stderr, "ELF parse failed: %s\n",
      elf_parse_result_name(res));
    return -1;
  }

  // Initialized the DWARF info.
  Dwarf_Error error = NULL;
  Dwarf_Debug dwarf = dwarf_init(prog_name, &error);
  if (dwarf == NULL) {
    free_elf(elf_buf);
    fprintf(stderr, "DWARF initialization failed: %s\n",
      dwarf_errmsg(error));
    dwarf_dealloc_error(NULL, error);
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    return -1;
  } else if (pid == 0) {
    /* Start the child process. */

    // Disable address space layout randomization.
    personality(ADDR_NO_RANDOMIZE);

    // Flag *this* process as the tracee.
    pt_trace_me();

    // Replace the current process with the
    // given program to debug. Only pass its
    // name to it.
    execv(prog_name, prog_argv);
  } else if (pid >= 1) {
    /* Parent process */

    /* Wait until the tracee has received the initial
       SIGTRAP. Don't handle the signal like in `wait_for_signal`. */
    int wait_status;
    int options = 0;
    waitpid(pid, &wait_status, options);

    // Now we can finally touch `store` 😄.
    *store = (Debugger) {
      .prog_name=prog_name,
      .pid=pid,
      .breakpoints=init_breakpoints(pid),
      .elf=elf_buf,
      /* `load_address` is initialized by `init_load_address`. */
      .load_address.value=0,
      .dwarf=dwarf,
      .files=init_source_files(),
      .history=init_history(),
    };
    init_load_address(store);
  }

  return 0;
}

SprayResult free_debugger(Debugger dbg) {
  free_source_files(dbg.files);
  free_breakpoints(dbg.breakpoints);
  dwarf_finish(dbg.dwarf);
  return free_elf(dbg.elf);
}

void run_debugger(Debugger dbg) {
  printf("🐛🐛🐛 %d 🐛🐛🐛\n", dbg.pid);

  x86_addr start_main = { 0 };
  SprayResult found_start = get_function_start_addr(dbg.dwarf, "main", &start_main);
  if (found_start == SP_OK) {
    enable_breakpoint(dbg.breakpoints, start_main);
    ExecResult exec_res = continue_execution(dbg);
    if (exec_res.type == SP_ERR) {
      print_exec_res(dbg, exec_res);
      free_debugger(dbg);
      return;
    }
    ExecResult wait_res = wait_for_signal(dbg);
    if (wait_res.type == SP_ERR) {
      print_exec_res(dbg, wait_res);
      free_debugger(dbg);
      return;
    }
  }

  print_current_source(dbg);

  char *line_buf = NULL;
  while ((line_buf = linenoise("spray> ")) != NULL) {
    handle_debug_command(&dbg, line_buf);
    linenoiseHistoryAdd(line_buf);
    linenoiseFree(line_buf);
  }

  if (free_debugger(dbg) == SP_ERR) {
    internal_error("Failed to unmap the executable from memory");
  }
}
