#include <stdio.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include "linenoise.h"

#define unused(x) (void) (x);

typedef struct {
  const char* prog_name;
  pid_t pid;
} dbg_t;

bool is_command(
  const char *restrict in,
  const char *restrict short_from,
  const char *restrict long_form
) {
  return (strcmp(in, short_from) == 0)
    || (strcmp(in, long_form) == 0);
}

void dbg_continue(dbg_t dbg) {
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
void dbg_cmd(dbg_t dbg, const char *line_buf) {
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
  } else {
    printf("I don't know that 🤔\n");
  }

  free(line);
}

void dbg_run(dbg_t dbg) {

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
    dbg_t debugger = { prog_name, pid };
    dbg_run(debugger);
  }

  return 0;
}
