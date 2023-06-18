/* x86_64 linux debugger. */

#include <stdio.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/user.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "magic.h"
#include "breakpoints.h"
#include "registers.h"

#include "linenoise.h"

typedef struct {
  const char *prog_name;
  pid_t pid;
  breakpoint *bps;
  size_t nbp;
} debugger;

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

void dbg_print_registers(pid_t pid) {
  for (size_t i = 0; i < N_REGISTERS; i++) {
    reg_descriptor desc = reg_descriptors[i];
    printf("\t%8s 0x%016lx", desc.name, get_register_value(pid, desc.r));
    // Always put two registers on the same line.
    if (i % 2 == 1) { printf("\n"); }
  }
}

void dbg_break(debugger* dbg, void *addr) {
  assert(dbg != NULL);

  breakpoint bp = {
    .pid = dbg->pid,
    .addr = addr,
    .is_enabled = false,
    .orig_data = 0x00,
  };

  enable_breakpoint(&bp);
  
  dbg->nbp += 1;
  dbg->bps = (breakpoint*) realloc (dbg->bps, dbg->nbp * sizeof(breakpoint));
  assert(dbg->bps != NULL);
  dbg->bps[dbg->nbp - 1] = bp;
}

void dbg_continue(debugger dbg) {
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

uint64_t dbg_read_mem(pid_t pid, void *addr) {
  return ptrace(PTRACE_PEEKDATA, pid, addr, NULL);
}

void dbg_write_mem(pid_t pid, void *addr, uint64_t value) {
  uint64_t res = ptrace(PTRACE_POKEDATA, pid, addr, value);
  perror("memory write");
}

#define CMD_DELIM " \t"

/* Evaluate a debug command. Available commands are:
 * 
 *   (continue | c): continue execution until next breakpoint is hit. 
 *
 *   (break | b) <address>: set a breakpoint at <address>.
 *
 *   (register | r) (read | rd) <name>: read the value in register <name>.
 *
 *   (register | r) (write | wr) <name> <value>: write <value> to register <name>.
 *
 *   (register | r) (print | dump): read the values of all registers
 *
 *   (memory | m) <address> (read | rd)
 *
 *   (memory | m) <address> (write | wr) <value>
 *
 * Where <address> and <value> are validated with `strtol(..., 16)`. 
 * All valid register names can be found in the `reg_descriptors`
 * table in `src/registers.h`.
 *
 *
 */
void dbg_cmd(debugger* dbg, const char *line_buf) {
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

  if (cmd == NULL) {
    puts("Empty command 🤨");
  } else if (is_command(cmd, "c", "continue")) {
    dbg_continue(*dbg);
  } else if (is_command(cmd, "b", "break")) {
    // Pass `NULL` to `strtok_r` to continue scanning `line`.
    char *addr_str = strtok_r(NULL, CMD_DELIM, &saveptr);
    if (addr_str == NULL) {
      puts("Missing address after 'break' 😠");
    } else {
      char *addr_end = addr_str;  // Copy start.
      uint64_t addr = strtol(addr_str, &addr_end, 16);
      if (addr_str[0] != '\0' && *addr_end == '\0') {
        // Some number was parsed and the entire
        // string is valid.
        dbg_break(dbg, (void*) addr);
      } else {
        puts("Invalid address after 'break' 🤦");
      }
    }
  } else if (is_command(cmd, "r", "register")) {
    char *op_str = strtok_r(NULL, CMD_DELIM, &saveptr);
    if (op_str == NULL) {
      puts("Missing operation after 'register' 😠");
    } else {
      if (is_command(op_str, "rd", "read")) {
        char *name = strtok_r(NULL, CMD_DELIM, &saveptr);
        if (name == NULL) {
          puts("Missing name after 'register read' 😠");
        } else {
          x86_reg reg;
          bool found_register = get_register_from_name(name, &reg);
          if (found_register) {
            printf("%8s 0x%016lx\n", name,
              get_register_value(dbg->pid, reg));
          } else {
            printf("Invalid register name '%s' 🤦\n", name);
          }
        }
      } else if (is_command(op_str, "wr", "write")) {
        char *name = strtok_r(NULL, CMD_DELIM, &saveptr);
        if (name == NULL) {
          puts("Missing name after 'register write' 😠");
        } else {
          x86_reg reg;
          bool found_register = get_register_from_name(name, &reg);
          if (found_register) {
            char *value_str = strtok_r(NULL, CMD_DELIM, &saveptr);
            if (value_str == NULL) {
              printf("Missing value after 'register write %s' 😠\n", name);
            } else {
              char *value_end = value_str;
              uint64_t value = strtol(value_str, &value_end, 16);
              if (value_str[0] != '\0' && *value_end == '\0') {
                set_register_value(dbg->pid, reg, value);
                printf("%8s 0x%016lx (read after write)\n", name,
                  get_register_value(dbg->pid, reg));
              } else {
                printf("Invalid value after 'register write %s' 🤦\n", name);
              }
            }
          } else {
            printf("Invalid register name '%s' 🤦\n", name);
          }
        }
      } else if (is_command(op_str, "dump", "print")) {
        dbg_print_registers(dbg->pid);
      } else {
        puts("Invalid operation after 'register' 🤦");
      }
    }
  } else if (is_command(cmd, "m", "memory")) {
    char *addr_str = strtok_r(NULL, CMD_DELIM, &saveptr);
    uint64_t addr;
    if (addr_str == NULL) {
      printf("Missing address after 'memory' 😠\n");
    } else {
      char *addr_str_end = addr_str;
      uint64_t addr_buf = strtol(addr_str, &addr_str_end, 16);
      if (addr_str[0] != '\0' && *addr_str_end == '\0') {
        addr = addr_buf;
      } else {
        printf("Invalid address after 'memory' 🤦\n");
        goto cleanup;
      }
    }
    char *op_str = strtok_r(NULL, CMD_DELIM, &saveptr);
    if (op_str == NULL) {
      printf("Missing memory operation");
    } else if (is_command(op_str, "rd", "read")) {
      printf("0x%016lx\n",
        dbg_read_mem(dbg->pid, (void*) addr));
    } else if (is_command(op_str, "wr", "write")) {
      char *value_str = strtok_r(NULL, CMD_DELIM, &saveptr);
      if (value_str == NULL) {
        printf("Missing memory write value 😠\n");
      } else {
        uint64_t value;
        if (read_hex(value_str, &value)) {
          dbg_write_mem(dbg->pid, (void*) addr, value);
          printf("0x%016lx (read after write)\n",
            dbg_read_mem(dbg->pid, (void*) addr));
        } else {
          printf("Invalid valud for memory write 🤦\n");
        }
      }
    }
  } else {
    puts("I don't know that 🤔");
  }

  cleanup:
  free(line);
}

void dbg_run(debugger dbg) {
  // Suspend executaion until state change of
  // child process `pid`.
  int wait_status;
  int options = 0;
  waitpid(dbg.pid, &wait_status, options);

  char *line_buf = NULL;
  while ((line_buf = linenoise("spray> ")) != NULL) {
    dbg_cmd(&dbg, line_buf);
    linenoiseHistoryAdd(line_buf);
    linenoiseFree(line_buf);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: spray PROGRAM_NAME\n");
    return -1;
  }

  const char *prog_name = argv[1];
  (void) prog_name;

  pid_t pid = fork();
  if (pid == -1) {
    perror("Fork failed");
    return -1;
  } else if (pid == 0) {
    // Child process. Execute program here.

    // Disable address space layout randomization.
    personality(ADDR_NO_RANDOMIZE);

    /*
     * `ptrace(2)` is the debugger's best friend. It allows
     * reading registers, reading memory, single stepping etc.
     * 
     * `long ptrace(enum __ptrace_request request, pid_t pid,
     *     void *addr, void *addr);`
     *
     * `request` is what we'd like to do.
     * `pid` is the process ID of the traced process
     * `addr` is a memory address which is sometimes used
     * do designate an address in the tracee.
     * `data` is a request specific resource.
     * The return value provied information about the outcome.
     */

    // `PTRACE_TRACEME` signals that *this* is the process
    // to track. `pid`, `addr` and `data` are ignored.
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);

    // Execute the given program. First argument is
    // program to execute. The rest are the program's
    // arguments.
    // `exec(3)` replaced the current process image with
    // a new process image.
    execl(prog_name, prog_name, NULL);

  } else if (pid >= 1) {
    printf("Let's get to it! %d\n", pid);
    debugger debugger = {
      prog_name,
      pid,
      NULL,
      0,
    };
    dbg_run(debugger);
  }

  return 0;
}
