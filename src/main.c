#include <stdio.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "linenoise.h"

#define unused(x) (void) (x);

enum {
  INT3 = 0xcc,
  BTM_BYTE_MASK = 0xff,
};

typedef struct {
  pid_t pid;
  void *addr;
  bool is_enabled;
  uint8_t orig_data;
} breakpoint;

// Enable the given breakpoint by replacing the
// instruction at `addr` with `int 3` (0xcc). This
// will make the child raise `SIGTRAP` once the
// instruction is reached.
void break_enable(breakpoint* bp) {
  assert(bp != NULL);

  // Read and return a word at `bp->addr` in the tracee's memory.
  long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
  // Save the original bottom byte.
  bp->orig_data = (uint8_t) (data & BTM_BYTE_MASK);
  // Set the new bottom byte to `int 3`.
  long int3_data = ((data & ~BTM_BYTE_MASK) | INT3);
  // Update the word in the tracee's memory.
  ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, int3_data);

  bp->is_enabled = true;
}

// Disable a breakpoint, restoring the original instruction.
void break_disable(breakpoint* bp) {
  assert(bp != NULL);

  // `ptrace` only operatores on whole words, so we need
  // to read what's currently there first, then replace the
  // modified low byte and write it to the address.

  long modified_data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
  long restored_data = ((modified_data & ~BTM_BYTE_MASK) | bp->orig_data);
  ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, restored_data);

  bp->is_enabled = false;
}

typedef struct {
  const char* prog_name;
  pid_t pid;
  breakpoint* bps;
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

void dbg_break(debugger* dbg, void *addr) {
  assert(dbg != NULL);

  breakpoint bp = {
    .pid = dbg->pid,
    .addr = addr,
    .is_enabled = false,
    .orig_data = 0x00,
  };

  break_enable(&bp);
  
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

/* Evaluate a debug command. Available commands are:
 * 
 * continue: continue execution until next breakpoint is hit. 
 *
 */
void dbg_cmd(debugger dbg, const char *line_buf) {
  assert(line_buf != NULL);

  // Copy line_buf to allow modifying it.
  char *line = strdup(line_buf);
  assert(line != NULL);  // Only `NULL` if allocation failed.
  
  // Start parsing `line` word by word.
  // `strtok` advanced until the first non-delimiter
  // bytes is found. Hence, it will handle multiple
  // spaces, too.
  char *saveptr;
  char *cmd = strtok_r(line, " \t", &saveptr);

  if (cmd == NULL) {
    printf("Empty command 🤨\n");
  } else if (is_command(cmd, "c", "continue")) {
    dbg_continue(dbg);
  } else if (is_command(cmd, "b", "break")) {
    char *addr_str = strtok_r(line, " \t", &saveptr);
    if (addr_str == NULL) {
      printf("Missing address after break 😠");
    } else {
      char *addr_end = addr_str;  // Copy start.
      long addr = strtol(addr_str, &addr_end, 16);
      if (addr_str[0] == '\0' && *addr_end == '\0') {
        // Some number was parsed and the entire
        // string is valid.
        dbg_break(&dbg, (void*) addr);
      } else {
        printf("Invalid address after break 🤦");
      }
    }
  } else {
    printf("I don't know that 🤔\n");
  }

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
    dbg_cmd(dbg, line_buf);
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
