#include "debugger.h"
#include "magic.h"
#include "registers.h"

#include "linenoise.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/ptrace.h>
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

bool read_hex(char *restrict str, uint64_t *dest) {
  char *str_end = str;
  uint64_t value = strtol(str, &str_end, 16);
  if (str[0] != '\0' && *str_end == '\0') {
    *dest = value;
    return true;
  } else {
    return false;
  }
}

void exec_command_memory_read(pid_t pid, uint64_t addr) {
  printf("         0x%016lx\n",
    ptrace(PTRACE_PEEKDATA, pid, addr, NULL));
}

void exec_command_memory_write(pid_t pid, uint64_t addr, uint64_t value) {
  ptrace(PTRACE_POKEDATA, pid, addr, value);
  // Print readout of write result:
  printf("         0x%016lx (read after write)\n",
    ptrace(PTRACE_PEEKDATA, pid, addr, NULL));
}

void exec_command_register_read(pid_t pid, x86_reg reg, const char *restrict reg_name) {
  printf("%8s 0x%016lx\n",
    reg_name, get_register_value(pid, reg));
}

void exec_command_register_write(pid_t pid, x86_reg reg, const char *restrict reg_name, uint64_t value) {
  set_register_value(pid, reg, value);
  // Print readout of write result:
  printf("%8s 0x%016lx (read after write)\n", reg_name,
    get_register_value(pid, reg));
}

void exec_command_print(pid_t pid) {
  for (size_t i = 0; i < N_REGISTERS; i++) {
    reg_descriptor desc = reg_descriptors[i];
    printf("\t%8s 0x%016lx", desc.name, get_register_value(pid, desc.r));
    // Always put two registers on the same line.
    if (i % 2 == 1) { printf("\n"); }
  }
}

void exec_command_break(Debugger* dbg, uint64_t addr) {
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
  errno = 0;
  // Continue child process execution.
  ptrace(PTRACE_CONT, dbg.pid, NULL, NULL);

  if (errno == ESRCH) {
    printf("The process is dead 😭\n");
  }

  // Again, pause it until it receives another signal.
  int wait_status;
  int options = 0;
  waitpid(dbg.pid, &wait_status, options);
}

#define CMD_DELIM " \t"

void handle_debug_command(Debugger* dbg, const char *line_buf) {
  assert(dbg != NULL);
  assert(line_buf != NULL);

  // Copy line_buf to allow modifying it.
  char *line = strdup(line_buf);
  assert(line != NULL);  // Only `NULL` if allocation failed.
  
  // Start parsing `line` word by word.
  // `strtok` advanced until the first non-delimiter
  // bytes is found. Hence, it will handle multiple
  // spaces, too.
  char *saveptr;
  char *cmd = strtok_r(line, CMD_DELIM, &saveptr);

  do {
    
    if (cmd == NULL) {
      empty_command_error();
    } else if (is_command(cmd, "c", "continue")) {
      exec_command_continue(*dbg);
    } else if (is_command(cmd, "b", "break")) {
      // Pass `NULL` to `strtok_r` to continue scanning `line`.
      char *addr_str = strtok_r(NULL, CMD_DELIM, &saveptr);
      if (addr_str == NULL) {
        missing_error(error_messages[BREAK_ADDR]);
      } else {
        uint64_t addr;
        if (read_hex(addr_str, &addr)) {
          exec_command_break(dbg, addr);
        } else {
          invalid_error(error_messages[BREAK_ADDR]);
        }
      }
    } else if (is_command(cmd, "r", "register")) {
      char *name = strtok_r(NULL, CMD_DELIM, &saveptr);
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

      char *op_str = strtok_r(NULL, CMD_DELIM, &saveptr);
      if (op_str == NULL) {
        missing_error(error_messages[REGISTER_OPERATION]);
      } else {
        if (is_command(op_str, "rd", "read")) {
          /* Read */
          exec_command_register_read(dbg->pid, reg, name);
        } else if (is_command(op_str, "wr", "write")) {
          /* Write */
          char *value_str = strtok_r(NULL, CMD_DELIM, &saveptr);
          if (value_str == NULL) {
            missing_error(error_messages[REGISTER_WRITE_VALUE]);
          } else {
            uint64_t value;
            if (read_hex(value_str, &value)) {
              exec_command_register_write(dbg->pid, reg, name, value);
            } else {
              invalid_error(error_messages[REGISTER_WRITE_VALUE]);
            }
          }
        } else {
          invalid_error(error_messages[REGISTER_OPERATION]);
        }
      }
    } else if (is_command(cmd, "m", "memory")) {
      char *addr_str = strtok_r(NULL, CMD_DELIM, &saveptr);
      uint64_t addr;
      if (addr_str == NULL) {
        missing_error(error_messages[MEMORY_ADDR]);
        break;
      } else {
        char *addr_str_end = addr_str;
        uint64_t addr_buf = strtol(addr_str, &addr_str_end, 16);
        if (addr_str[0] != '\0' && *addr_str_end == '\0') {
          addr = addr_buf;
        } else {
          invalid_error(error_messages[MEMORY_ADDR]);
          break;
        }
      }

      char *op_str = strtok_r(NULL, CMD_DELIM, &saveptr);
      if (op_str == NULL) {
        missing_error(error_messages[MEMORY_OPERATION]);
      } else if (is_command(op_str, "rd", "read")) {
        /* Read */
        exec_command_memory_read(dbg->pid, addr);
      } else if (is_command(op_str, "wr", "write")) {
        char *value_str = strtok_r(NULL, CMD_DELIM, &saveptr);
        if (value_str == NULL) {
          missing_error(error_messages[MEMORY_WRITE_VALUE]);
        } else {
          uint64_t value;
          if (read_hex(value_str, &value)) {
            exec_command_memory_write(dbg->pid, addr, value);
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

int setup_debugger(const char *prog_name, Debugger* dest) {
  assert(dest != NULL);

  pid_t pid = fork();
  if (pid == -1) {
    return -1;
  } else if (pid == 0) {
    /* Start the child process. */

    // Disable address space layout randomization.
    personality(ADDR_NO_RANDOMIZE);

    // Flag *this* process as the tracee.
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);

    // Replace the current process with the
    // given program to debug. Only pass its
    // name to it.
    execl(prog_name, prog_name, NULL);
  } else if (pid >= 1) {
    /* Parent process */
    printf("🐛🐛🐛 %d 🐛🐛🐛\n", pid);
    *dest = (Debugger) { prog_name, pid, NULL, 0 };
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
