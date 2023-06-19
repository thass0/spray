#include "debugger.h"
#include "magic.h"
#include "registers.h"
#include "ptrace.h"

#include "linenoise.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/personality.h>


/* NOTE: Command error messages are stored
 * in the following enum and array because
 * most of them (all except for one) are
 * used twice. This ensures they are always
 * spelled the same. */

enum error_codes {
  BREAK_ADDR,
  REGISTER_NAME,
  REGISTER_OPERATION,
  REGISTER_WRITE_VALUE,
  MEMORY_ADDR,
  MEMORY_OPERATION,
  MEMORY_WRITE_VALUE,
};

static const char* error_messages[] = {
  [BREAK_ADDR]="address for break",
  [REGISTER_NAME]="register name for register operation",
  [REGISTER_OPERATION]="register operation",
  [REGISTER_WRITE_VALUE]="value for register write",
  [MEMORY_ADDR]="address for memory operation",
  [MEMORY_OPERATION]="memory operation",
  [MEMORY_WRITE_VALUE]="value for memory write",
};

static inline void invalid_error(const char *restrict what) {
  printf("🤦 Invalid %s\n", what);
}

static inline void missing_error(const char *restrict what) {
  printf("😠 Missing %s\n", what);
}

static inline void empty_command_error(void) {
  printf("Empty command 🤨\n");
}

static inline void unknown_cmd_error(void) {
  printf("I don't know that 🤔\n");
}

bool is_command(
  const char *restrict in,
  const char *restrict short_from,
  const char *restrict long_form
) {
  return (strcmp(in, short_from) == 0)
    || (strcmp(in, long_form) == 0);
}

bool read_hex(char *restrict str, uint64_t *store) {
  char *str_end = str;
  uint64_t value = strtol(str, &str_end, 16);
  if (str[0] != '\0' && *str_end == '\0') {
    *store = value;
    return true;
  } else {
    return false;
  }
}

/* Get and set the instruction pointer. */
x86_addr get_ip(pid_t pid) {
  return (x86_addr) { get_register_value(pid, rip).value };
}

void set_ip(pid_t pid, x86_addr ip) {
  set_register_value(pid, rip, (x86_word) { ip.value });
}

void wait_for_signal(pid_t pid) {
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
  waitpid(pid, &wait_status, options);

  /* Display some info about the state-change which
   * has just stopped the tracee. This helps grasp
   * what state the tracee is in now that we can
   * inspect it. */

  // Did the tracee terminate normally?
  if (WIFEXITED(wait_status)) {
    printf("Child exited with code %d\n",
      WEXITSTATUS(wait_status));
  }
  // Was the tracee terminated by a signal?
  else if (WIFSIGNALED(wait_status)) {
    printf("Child was terminated by signal %s\n",
      sigabbrev_np(WTERMSIG(wait_status)));
  }
  // Did the tracee receive a `SIGCONT` signal?
  else if (WIFCONTINUED(wait_status)) {
    printf("Child was resumed\n");
  }
  // Was the tracee stopped by another signal?
  else if (WIFSTOPPED(wait_status)) {
    printf("Child was stopped by SIG%s\n",
      sigabbrev_np(WSTOPSIG(wait_status)));
  }
}

/* Set `store_idx` to the breakpoints index if there
 * is a breakpoint with the given address. */
bool find_bp_at_addr(Debugger dbg, x86_addr location, size_t *store_idx) {
  size_t i = 0;
  for ( ; i < dbg.nbp; i++) {
    if (dbg.bps[i].addr.value == location.value) {
      break;
    }
  }

  if (i == dbg.nbp) {
    /* Reached end without match an address. */
    return false;
  } else {
    *store_idx = i;
    return true;
  }
}

void step_over_breakpoint(Debugger dbg) {
  /* Subtract one because execution will go past
   * the breakpoint before stopping. */
  x86_addr possible_bp_addr = { get_ip(dbg.pid).value - 1 };

  size_t bp_idx;
  if (find_bp_at_addr(dbg, possible_bp_addr, &bp_idx)) {
    Breakpoint *bp = &dbg.bps[bp_idx];

    if (bp->is_enabled) {
      /* Reset the tracee instruction pointer to the
       * instruction that raised the SIGTRAP. */
      x86_addr prev_inst_addr = possible_bp_addr;
      set_ip(dbg.pid, prev_inst_addr);

      /* Disable the breakpoint, run only the original
       * instruction and then reenable it before we
       * continue. */
      disable_breakpoint(bp);
      pt_single_step(dbg.pid);
      // Wait until the tracee has received
      // the signal to single step.
      wait_for_signal(dbg.pid);
      enable_breakpoint(bp);
    }
  }
}

void exec_command_memory_read(pid_t pid, x86_addr addr) {
  x86_word read = { 0 };
  pt_call_result res = pt_read_memory(pid, addr, &read);
  unused(res);
  printf("         0x%016lx\n", read.value);
}

void exec_command_memory_write(pid_t pid, x86_addr addr, x86_word word) {
  pt_write_memory(pid, addr, word);
  // Print readout of write result:
  x86_word stored = { 0 };
  pt_call_result res = pt_read_memory(pid, addr, &stored);
  unused(res);
  printf("         0x%016lx (read after write)\n", stored.value);
}

void exec_command_register_read(pid_t pid, x86_reg reg, const char *restrict reg_name) {
  x86_word reg_word = get_register_value(pid, reg);
  printf("%8s 0x%016lx\n",
    reg_name, reg_word.value);
}

void exec_command_register_write(pid_t pid, x86_reg reg, const char *restrict reg_name, x86_word word) {
  set_register_value(pid, reg, word);
  // Print readout of write result:
  x86_word written = get_register_value(pid, reg);
  printf("%8s 0x%016lx (read after write)\n", reg_name,
    written.value);
}

void exec_command_print(pid_t pid) {
  for (size_t i = 0; i < N_REGISTERS; i++) {
    reg_descriptor desc = reg_descriptors[i];
    x86_word reg_word = get_register_value(pid, desc.r);
    printf("\t%8s 0x%016lx", desc.name, reg_word.value);
    // Always put two registers on the same line.
    if (i % 2 == 1) { printf("\n"); }
  }
}

void exec_command_break(Debugger* dbg, x86_addr addr) {
  assert(dbg != NULL);

  Breakpoint bp = {
    .pid = dbg->pid,
    .addr = addr,
    .is_enabled = false,
    .orig_data = 0x00,
  };

  enable_breakpoint(&bp);
  
  dbg->nbp += 1;
  dbg->bps = (Breakpoint*) realloc (dbg->bps, dbg->nbp * sizeof(Breakpoint));
  assert(dbg->bps != NULL);
  dbg->bps[dbg->nbp - 1] = bp;
}

void exec_command_continue(Debugger dbg) {
  step_over_breakpoint(dbg);

  // Continue child process execution.
  errno = 0;
  pt_continue_execution(dbg.pid);
  if (errno == ESRCH) {
    printf("The process is dead 😭\n");
    return;
  }

  wait_for_signal(dbg.pid);
}

static inline char *get_next_command_token(char *restrict line) {
  return strtok(line, " \t");
}

void handle_debug_command(Debugger* dbg, const char *line_buf) {
  assert(dbg != NULL);
  assert(line_buf != NULL);

  // Copy line_buf to allow modifying it.
  char *line = strdup(line_buf);
  assert(line != NULL);  // Only `NULL` if allocation failed.
  
  const char *cmd = get_next_command_token(line);

  do {
    if (cmd == NULL) {
      empty_command_error();
    } else if (is_command(cmd, "c", "continue")) {
      exec_command_continue(*dbg);
    } else if (is_command(cmd, "b", "break")) {
      // Pass `NULL` to `strtok_r` to continue scanning `line`.
      char *addr_str = get_next_command_token(NULL);
      if (addr_str == NULL) {
        missing_error(error_messages[BREAK_ADDR]);
      } else {
        x86_addr addr;
        if (read_hex(addr_str, &addr.value)) {
          exec_command_break(dbg, addr);
        } else {
          invalid_error(error_messages[BREAK_ADDR]);
        }
      }
    } else if (is_command(cmd, "r", "register")) {
      const char *name = get_next_command_token(NULL);
      x86_reg reg;
      if (name == NULL) {
        missing_error(error_messages[REGISTER_NAME]);
        break;
      } else if (is_command(name, "dump", "print")) {
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
          invalid_error(error_messages[REGISTER_NAME]);
          break;
        }
      }

      const char *op_str = get_next_command_token(NULL);
      if (op_str == NULL) {
        missing_error(error_messages[REGISTER_OPERATION]);
      } else {
        if (is_command(op_str, "rd", "read")) {
          /* Read */
          exec_command_register_read(dbg->pid, reg, name);
        } else if (is_command(op_str, "wr", "write")) {
          /* Write */
          char *value_str = get_next_command_token(NULL);
          if (value_str == NULL) {
            missing_error(error_messages[REGISTER_WRITE_VALUE]);
          } else {
            x86_word word;
            if (read_hex(value_str, &word.value)) {
              exec_command_register_write(dbg->pid, reg, name, word);
            } else {
              invalid_error(error_messages[REGISTER_WRITE_VALUE]);
            }
          }
        } else {
          invalid_error(error_messages[REGISTER_OPERATION]);
        }
      }
    } else if (is_command(cmd, "m", "memory")) {
      char *addr_str = get_next_command_token(NULL);
      x86_addr addr;
      if (addr_str == NULL) {
        missing_error(error_messages[MEMORY_ADDR]);
        break;
      } else {
        x86_addr addr_buf;
        if (read_hex(addr_str, &addr_buf.value)) {
          addr = addr_buf;
        } else {
          invalid_error(error_messages[MEMORY_ADDR]);
          break;
        }
      }

      const char *op_str = get_next_command_token(NULL);
      if (op_str == NULL) {
        missing_error(error_messages[MEMORY_OPERATION]);
      } else if (is_command(op_str, "rd", "read")) {
        /* Read */
        exec_command_memory_read(dbg->pid, addr);
      } else if (is_command(op_str, "wr", "write")) {
        char *value_str = get_next_command_token(NULL);
        if (value_str == NULL) {
          missing_error(error_messages[MEMORY_WRITE_VALUE]);
        } else {
          x86_word word;
          if (read_hex(value_str, &word.value)) {
            exec_command_memory_write(dbg->pid, addr, word);
          } else {
            invalid_error(error_messages[MEMORY_WRITE_VALUE]);
          }
        }
      }
    } else {
      unknown_cmd_error();
    }
  } while (0); /* Only run this block once. The
   * loop is only used to make `break` available
   * for  skipping subsequent steps on error. */

  free(line);
}

int setup_debugger(const char *prog_name, Debugger* store) {
  assert(store != NULL);

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
    execl(prog_name, prog_name, NULL);
  } else if (pid >= 1) {
    /* Parent process */
    printf("🐛🐛🐛 %d 🐛🐛🐛\n", pid);
    *store = (Debugger) { prog_name, pid, NULL, 0 };
  }

  return 0;
}

void run_debugger(Debugger dbg) {

  // Suspend executaion until state change of child process `pid`.
  int wait_status;
  int options = 0;
  waitpid(dbg.pid, &wait_status, options);

  char *line_buf = NULL;
  while ((line_buf = linenoise("spray> ")) != NULL) {
    handle_debug_command(&dbg, line_buf);
    linenoiseHistoryAdd(line_buf);
    linenoiseFree(line_buf);
  }
}
