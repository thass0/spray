#include "debugger.h"
#include "backtrace.h"
#include "magic.h"
#include "ptrace.h"
#include "registers.h"
#include "print_source.h"

#include "linenoise.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <limits.h>		/* `UINT_MAX` */
#include <sys/wait.h>
#include <sys/personality.h>


// ========================
// PC and Address Utilities
// ========================


void print_info(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  vprintf(fmt, argp);
  printf("\n");
  va_end(argp);  
}


/* Get the program counter. */
real_addr get_pc(pid_t pid) {
  uint64_t store;
  SprayResult res = get_register_value(pid, rip, &store);
  if (res == SP_OK) {
    return (real_addr){ store };
  } else {
    return (real_addr){ 0 };
  }
}

/* Get the program counter and remove any offset which is
   added to the physical process counter in PIEs. The DWARF
   debug info doesn't know about this offset. */
dbg_addr get_dbg_pc(Debugger *dbg) {
  assert(dbg != NULL);

  real_addr real_pc = get_pc(dbg->pid);
  return real_to_dbg(dbg->load_address, real_pc);
}

/* Set the program counter. */
void set_pc(pid_t pid, real_addr pc) {
  set_register_value(pid, rip, pc.value);
}

bool is_user_breakpoint(Debugger *dbg) {
  /*
    The debugger might insert breakpoints internally to implement
    different kinds of stepping behavior. Such breakpoints are
    always deleted right after they have been hit. Thus, at any
    stopped state, it's possible to determine if the breakpoint
    that lead to the state was created by the user, based on
    whether it still exists.
  */

  return lookup_breakpoint(dbg->breakpoints, get_pc(dbg->pid));
}

void print_current_source(Debugger *dbg) {
  assert(dbg != NULL);

  dbg_addr pc = get_dbg_pc(dbg);
  const DebugSymbol *sym = sym_by_addr(pc, dbg->info);

  const Position *pos = sym_position(sym, dbg->info);
  const char *filepath = sym_filepath(sym, dbg->info);

  if (pos != NULL && filepath != NULL) {
    if (is_user_breakpoint(dbg)) {
      printf("Hit breakpoint at address " ADDR_FORMAT " in ",
	     get_pc(dbg->pid).value);
      print_as_relative_filepath(filepath);
      printf("\n");
    }

    SprayResult res = print_source(filepath, pos->line, 3);
    if (res == SP_ERR) {
      repl_err("Failed to read source file %s. Can't print source", filepath);
    }
  } else {
    repl_err("No source info for PC " ADDR_FORMAT, pc.value);
  }
}


// =============================
// Stepping and Breakpoint Logic
// =============================

SprayResult wait_for_signal(Debugger *dbg);

/*
 Execute the instruction at the breakpoints location
 and stop the tracee again.
*/
SprayResult single_step_breakpoint(Debugger *dbg) {
  assert(dbg != NULL);

  real_addr pc_address = get_pc(dbg->pid);

  if (lookup_breakpoint(dbg->breakpoints, pc_address)) {
    /* Disable the breakpoint, run the original instruction and stop. */
    disable_breakpoint(dbg->breakpoints, pc_address);
    pt_single_step(dbg->pid);
    SprayResult res = wait_for_signal(dbg);
    enable_breakpoint(dbg->breakpoints, pc_address);
    return res;
  } else {
    return SP_OK;
  }
}

/* Continue execution of child process. If the PC is currently
   hung up on a breakpoint then that breakpoint is stepped-over. */
SprayResult continue_execution(Debugger *dbg) {
  assert(dbg != NULL);

  single_step_breakpoint(dbg);

  errno = 0;
  pt_continue_execution(dbg->pid);

  /* Is the process still alive? */
  if (errno == ESRCH) {
    printf("The process is dead\n");
    return SP_ERR;
  }

  return SP_OK;
}

void handle_sigtrap(Debugger *dbg, siginfo_t siginfo) {
  assert(dbg != NULL);

  /* Did the tracee hit a breakpoint? */
  if (
    siginfo.si_code == SI_KERNEL ||
    siginfo.si_code == TRAP_BRKPT
  ) {
    /* Go back to real breakpoint address. */
    real_addr pc = get_pc(dbg->pid);
    set_pc(dbg->pid, (real_addr) {pc.value - 1});
  }
}

SprayResult wait_for_signal(Debugger *dbg) {
  assert(dbg != NULL);

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
  int options = 0;  /* Normal behavior. */
  waitpid(dbg->pid, &wait_status, options);

  /* Display some info about the state-change which
   * has just stopped the tracee. This helps grasp
   * what state the tracee is in now that we can
   * inspect it. */

  // Did the tracee terminate normally?
  if (WIFEXITED(wait_status)) {
    print_info("Child exited with code %d", WEXITSTATUS(wait_status));
    /* Return an error, because the debugger cannot continue now. */
    return SP_ERR;
  }
  // Was the tracee terminated by a signal?
  else if (WIFSIGNALED(wait_status)) {
    print_info("Child was terminated by signal SIG%s",
	       sigabbrev_np(WTERMSIG(wait_status)));
    return SP_ERR;
  }
  // Did the tracee receive a `SIGCONT` signal?
  else if (WIFCONTINUED(wait_status)) {
    print_info("Child was resumed");
    return SP_OK;
  }
  // Was the tracee stopped by another signal?
  else if (WIFSTOPPED(wait_status)) {
    siginfo_t siginfo = { 0 };
    pt_get_signal_info(dbg->pid, &siginfo);

    switch (siginfo.si_signo) {
    case SIGSEGV:
      print_info("Child was stopped by a segmentation fault, reason %d", siginfo.si_code);
      return SP_OK;
    case SIGTRAP:
      handle_sigtrap(dbg, siginfo);

      /*
       If `siginfo.si_code == SI_KERNEL || siginfo.si_code == TRAP_BRKPT`,
       then this signal was caused by a breakpoint. See the `siginfo_t`
       man-page for more.
      */

      return SP_OK;
    case SIGWINCH:
      /*
	Ignore changes in window size by telling the
	tracee to continue in that case and then wait for
	the next interesting signal.
      */
      continue_execution(dbg);
      return wait_for_signal(dbg);
    default:
      print_info("Child was stopped by SIG%s", sigabbrev_np(WSTOPSIG(wait_status)));
      return SP_OK;
    }
  } else {
    repl_err("Received invalid wait status %d", wait_status);
    return SP_ERR;
  }
}

SprayResult single_step_instruction(Debugger *dbg) {
  assert(dbg != NULL);

  if (lookup_breakpoint(dbg->breakpoints, get_pc(dbg->pid))) {
    single_step_breakpoint(dbg);
    return SP_OK;
  } else {
    pt_single_step(dbg->pid);
    return wait_for_signal(dbg);
  }
}

/* Set a breakpoint on the current return address.
   Used for source-level stepping. Returns whether or
   not the breakpoint must be removed again after use.
   If it was to be removed,  the value of `return_address`
   is set to the address where the breakpoint was created. */
bool set_return_address_breakpoint(Breakpoints *breakpoints, pid_t pid, real_addr *return_address) {
  assert(breakpoints != NULL);
  assert(return_address != NULL);

  /* The return address is stored 8 bytes after the
     start of the stack frame. This is where we want
     to set a breakpoint. */
  uint64_t frame_pointer = 0;
  SprayResult res = get_register_value(pid, rbp, &frame_pointer);
  assert(res == SP_OK);

  real_addr return_address_location = { frame_pointer + 8 };
  pt_read_memory(pid, return_address_location, &return_address->value);

  bool remove_transient_breakpoint = false;
  if (!lookup_breakpoint(breakpoints, *return_address)) {
    enable_breakpoint(breakpoints, *return_address);
    remove_transient_breakpoint = true;
  }

  return remove_transient_breakpoint;
}

/* Step outside of the current function. */
SprayResult step_out(Debugger *dbg) {
  assert(dbg != NULL);

  real_addr return_address = { 0 };
  bool remove_internal_breakpoint =
    set_return_address_breakpoint(dbg->breakpoints,
				  dbg->pid,
				  &return_address);

  continue_execution(dbg);
  SprayResult res = wait_for_signal(dbg);

  if (remove_internal_breakpoint) {
    disable_breakpoint(dbg->breakpoints, return_address);
  }

  return res;
}

/* Single step instructions until the line number has changed. */
SprayResult single_step_line(Debugger *dbg) {
  assert(dbg != NULL);

  const Position *pos = addr_position(get_dbg_pc(dbg), dbg->info);
  if (pos == NULL) {
    printf("Failed to find current line");
    return SP_ERR;
  }

  uint32_t init_line = pos->line;

  /*
    Single step instructions until we find a valid line
    with a different line number than before.
  */
  unsigned n_instruction_steps = 0;
  while (!pos->is_exact || pos->line == init_line) {
    if (single_step_instruction(dbg) == SP_ERR)
      return SP_ERR;

    n_instruction_steps ++;

    /*
     Should we continue searching? We stop after
     a certain number of attempts has been made.
    */
    pos = addr_position(get_dbg_pc(dbg), dbg->info);
    if (pos == NULL || n_instruction_steps >= SINGLE_STEP_SEARCH_LIMIT) {
      repl_err("Failed to find another line to step to");
      return SP_ERR;
    }
  }

  return SP_OK;
}

/* Step to the next line. Don't step into functions. */
SprayResult step_over(Debugger *dbg) {
  assert(dbg != NULL);

  /*
   This functions sets breakpoints all over the current DWARF
   subprogram except for the next line so that we stop right
   after executing the code in it.
  */

  const DebugSymbol *func = sym_by_addr(get_dbg_pc(dbg), dbg->info);
  if (func == NULL) {
    repl_err("Failed to find current function");
    return SP_ERR;
  }

  real_addr *to_del = NULL;
  size_t n_to_del = 0;

  SprayResult set_res =
    set_step_over_breakpoints(func,
			      dbg->info,
			      dbg->load_address,
			      dbg->breakpoints,
			      &to_del,
			      &n_to_del);
  if (set_res == SP_ERR) {
    repl_err("Failed to set breakpoints in current scope");
    return SP_ERR;
  }

  real_addr return_address = { 0 };
  bool remove_internal_breakpoint =
    set_return_address_breakpoint(dbg->breakpoints,
				  dbg->pid,
				  &return_address);

  continue_execution(dbg);
  SprayResult exec_res = wait_for_signal(dbg);

  for (size_t i = 0; i < n_to_del; i++) {
    disable_breakpoint(dbg->breakpoints, to_del[i]);
  }
  free(to_del);

  if (remove_internal_breakpoint) {
    disable_breakpoint(dbg->breakpoints,
		       return_address);
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

void execmd_print_memory(pid_t pid, real_addr addr, PrintFilter filter) {
  uint64_t read = { 0 };
  SprayResult res = pt_read_memory(pid, addr, &read);
  if (res == SP_OK) {
    printf(MEM_READ_INDENT);
    print_filtered(read, default_filter(filter, PF_BYTES));
    printf("\n");
  } else {
    repl_err("Failed to read from child memory at address " ADDR_FORMAT, addr.value);
  }
}

void execmd_set_memory(pid_t pid, real_addr addr, uint64_t word, PrintFilter filter) {
  SprayResult write_res = pt_write_memory(pid, addr, word);
  if (write_res == SP_ERR) {
    repl_err("Failed to write to child memory at address " ADDR_FORMAT, addr.value);
    return;
  }

  /* Print a readout of the write's result. */
  uint64_t stored = 0;
  SprayResult read_res = pt_read_memory(pid, addr, &stored);
  if (read_res == SP_OK) {
    printf(MEM_READ_INDENT);
    print_filtered(stored, default_filter(filter, PF_BYTES));
    printf(" " WRITE_READ_MSG "\n");
  } else {
    repl_err("Failed to read from child memory to confirm "
	      "a write at address " ADDR_FORMAT, addr.value);
  }
}

void execmd_print_register(pid_t pid,
			   x86_reg reg,
			   const char *restrict reg_name,
			   PrintFilter filter) {
  uint64_t reg_word = 0;
  SprayResult res = get_register_value(pid, reg, &reg_word);
  if (res == SP_OK) {
    printf("%8s ", reg_name);
    print_filtered(reg_word, default_filter(filter, PF_BYTES));
    printf("\n");
  } else {
    repl_err("Failed to read from child register '%s'", get_name_from_register(reg));
  }
}

void execmd_set_register(pid_t pid,
			 x86_reg reg,
			 const char *restrict reg_name,
			 uint64_t word,
			 PrintFilter filter) {
  SprayResult write_res = set_register_value(pid, reg, word);
  if (write_res == SP_ERR) {
    repl_err("Failed to write to child register '%s'", get_name_from_register(reg));
  }

  /* Print readout of write result: */
  uint64_t written = 0;
  SprayResult read_res = get_register_value(pid, reg, &written);
  if (read_res == SP_OK) {
    printf("%8s ", reg_name);
    print_filtered(written, default_filter(filter, PF_BYTES));
    printf(" " WRITE_READ_MSG "\n");
  } else {
    repl_err("Failed to read from child register to "
	      "confirm a write to child register '%s'",
	      get_name_from_register(reg));
  }
}

void execmd_print_variable(Debugger *dbg, const char *var_name, PrintFilter filter) {
  assert(dbg != NULL);
  assert(var_name != NULL);

  RuntimeVariable *var = init_var(get_dbg_pc(dbg),
				  dbg->load_address,
				  var_name,
				  dbg->pid,
				  dbg->info);

  if (var == NULL) {
    repl_err("Failed to find a variable called %s", var_name);
    return;
  }

  uint64_t value = 0;
  SprayResult read_res = SP_ERR;

  /* Is this location a memory address? */
  if (is_addr_loc(var)) {
    real_addr loc_addr = var_loc_addr(var);
    read_res = pt_read_memory(dbg->pid, loc_addr, &value);
  }

  /* Is this location a register number? */
  if (is_reg_loc(var)) {
    x86_reg loc_reg = var_loc_reg(var);
    read_res = get_register_value(dbg->pid, loc_reg, &value);
  }

  if (read_res == SP_ERR) {
    repl_err("Found a variable %s, but failed to read its value", var_name);
  } else {
    printf(MEM_READ_INDENT);
    print_var_value(var, value, filter);
    printf(" (");
    print_var_loc(var);
    printf(")\n");
  }

  del_var(var);
}

#define SET_VAR_WRITE_ERR "Found a variable %s, but failed to write its value"
#define SET_VAR_READ_ERR "Wrote to variable %s, but failed to read its new value for validation"

void execmd_set_variable(Debugger *dbg,
			 const char *var_name,
			 uint64_t value,
			 PrintFilter filter) {
  assert(dbg != NULL);
  assert(var_name != NULL);

  RuntimeVariable *var = init_var(get_dbg_pc(dbg),
				  dbg->load_address,
				  var_name,
				  dbg->pid,
				  dbg->info);

  if (var == NULL) {
    repl_err("Failed to find a variable called %s", var_name);
    return;
  }

  uint64_t value_after = 0;
  SprayResult res = SP_ERR;

  /* Is this location a memory address? */
  if (is_addr_loc(var)) {
    real_addr loc_addr = var_loc_addr(var);
    res = pt_write_memory(dbg->pid, loc_addr, value);
    if (res == SP_ERR) {
      repl_err(SET_VAR_WRITE_ERR, var_name);
      del_var(var);
      return;
    }

    res = pt_read_memory(dbg->pid, loc_addr, &value_after);
    if (res == SP_ERR) {
      repl_err(SET_VAR_READ_ERR, var_name);
      del_var(var);
      return;
    }
  }

  /* Is this location a register number? */
  if (is_reg_loc(var)) {
    x86_reg loc_reg = var_loc_reg(var);
    res = set_register_value(dbg->pid, loc_reg, value);
    if (res == SP_ERR) {
      repl_err(SET_VAR_WRITE_ERR, var_name);
      del_var(var);
      return;
    }

    res = get_register_value(dbg->pid, loc_reg, &value_after);
    if (res == SP_ERR) {
      repl_err(SET_VAR_READ_ERR, var_name);
      del_var(var);
      return;
    }
  }

  /* Print the value that's been read after the write. */
  printf(MEM_READ_INDENT);
  print_var_value(var, value, filter);
  printf(" " WRITE_READ_MSG " (");
  print_var_loc(var);
  printf(")\n");

  del_var(var);
}

void execmd_break(Breakpoints *breakpoints, real_addr addr) {
  assert(breakpoints != NULL);
  enable_breakpoint(breakpoints, addr);
}

void execmd_delete(Breakpoints *breakpoints, real_addr addr) {
  assert(breakpoints != NULL);
  disable_breakpoint(breakpoints, addr);
}

/* Execute the instruction at the current breakpoint,
   continue the tracee and wait until it receives the
   next signal. */
void execmd_continue(Debugger *dbg) {
  assert(dbg != NULL);

  if (continue_execution(dbg) == SP_OK)
    if (wait_for_signal(dbg) == SP_OK)
      print_current_source(dbg);
}

void execmd_inst(Debugger *dbg) {
  assert(dbg != NULL);

  if (single_step_instruction(dbg) == SP_OK)
    print_current_source(dbg);
}

void execmd_leave(Debugger *dbg) {
  assert(dbg != NULL);

  if (step_out(dbg) == SP_OK)
    print_current_source(dbg);
}

void execmd_step(Debugger *dbg) {
  assert(dbg != NULL);

  /* Single step instructions until the line number has changed. */
  if (single_step_line(dbg) == SP_OK)
    print_current_source(dbg);
}

void execmd_next(Debugger *dbg) {
  assert(dbg != NULL);

  if (step_over(dbg) == SP_OK)
    print_current_source(dbg);
}

void execmd_backtrace(Debugger *dbg) {
  assert(dbg != NULL);

  CallFrame *backtrace = init_backtrace(get_dbg_pc(dbg),
					dbg->load_address,
					dbg->pid,
					dbg->info);
  if (backtrace == NULL) {
    repl_err("Failed to determine backtrace");
  } else {
    print_backtrace(backtrace);
  }
  free_backtrace(backtrace);
}


// ===============
// Command Parsing
// ===============

static inline const char *next_token(char *const *tokens, size_t *i) {
  assert(i != NULL);
  const char *ret = tokens[*i];
  if (ret  == NULL) {
    return NULL;
  } else {
    *i += 1;
    return ret;
  }
}

static inline bool end_of_tokens(char *const *tokens, size_t i) {
  if (tokens[i] == NULL) {
    return true;
  } else {
    repl_err("Trailing characters in command");
    return false;
  }
}

bool is_command(const char *restrict in,
		char short_form,
		const char *restrict long_form) {
  if (in != NULL) {
    return (strlen(in) == 1 && in[0] == short_form)
      || (strcmp(in, long_form) == 0);
  } else {
    return false;
  }
}

SprayResult parse_num(const char *restrict str, uint64_t *store, uint8_t base) {
  char *str_end;
  uint64_t value = strtol(str, &str_end, base);
  if (str[0] != '\0' && *str_end == '\0') {
    *store = value;
    return SP_OK;
  } else {
    return SP_ERR;
  }  
}

SprayResult parse_base16(const char *restrict str, uint64_t *store) {
  return parse_num(str, store, 16);
}

SprayResult parse_base10(const char *restrict str, uint64_t *store) {
  return parse_num(str, store, 10);
}

bool is_valid_identifier(const char *ident) {
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
                      ident,
                      0,     // We are not interested
                      NULL,  // in any sub-expressions.
                      0);
  regfree(&ident_regex);
  if (match == 0) {
    return true;
  } else {
    return false;
  }
}

SprayResult is_file_with_line(const char *file_line) {
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
                                 dbg_addr *dest) {
  assert(location != NULL);
  assert(dest != NULL);

  if (is_valid_identifier(location)){
    const DebugSymbol *func = sym_by_name(location, dbg.info);
    return function_start_addr(func, dbg.info, dest);
  } else if (parse_base16(location, &dest->value) == SP_OK) {
    return SP_OK;
  } else if (is_file_with_line(location) == SP_OK) {
    char *location_cpy = strdup(location);
    const char *filepath = strtok(location_cpy, ":");
    assert(filepath != NULL);  // OK since `location` was validated.

    unsigned lineno = 0;
    SprayResult res = parse_lineno(strtok(NULL, ":"), &lineno);
    if (res == SP_ERR) {
      free(location_cpy);
      return SP_ERR;
    }

    SprayResult addr_res = addr_at(filepath, lineno, dbg.info, dest);
    free(location_cpy);
    return addr_res;
  } else {
    return SP_ERR;
  }
}

char **get_command_tokens(const char *line) {
  enum { TOKENS_ALLOC=8 };
  size_t n_alloc = TOKENS_ALLOC;
  char **tokens = calloc(n_alloc, sizeof(*tokens));
  assert(tokens != NULL);

  char *line_buf = strdup(line);
  assert(line_buf != NULL);

  const char *token = strtok(line_buf, " \t");
  size_t i = 0;

  while (token != NULL) {
    if (i >= n_alloc) {
      n_alloc += TOKENS_ALLOC;
      tokens = realloc(tokens, sizeof(*tokens) * n_alloc);
      assert(tokens != NULL);
    }
    char *token_cpy = strdup(token);
    tokens[i] = token_cpy;
    token = strtok(NULL, " \t");
    i ++;
  }

  if (i >= n_alloc) {
    n_alloc += 1;
    tokens = realloc (tokens, sizeof(*tokens) * n_alloc);
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

bool is_filter_delim(const char *delim) {
  return delim != NULL && delim[0] == '|' && delim[1] == '\0';
}

void warn_register_name_conflict(const char *ident) {
  assert(ident != NULL);

  x86_reg _reg_buf = 0;	/* Not used. */
  bool valid_reg = get_register_from_name(ident, &_reg_buf);
  if (valid_reg) {
    repl_warn("The variable name '%s' is also the name of a register", ident);
    repl_hint("All register names start with a '%%'. Use '%%%s' to "
	       "access the '%s' register instead",
	       ident, ident);
  }
}

void handle_debug_command_tokens(Debugger* dbg, char *const *tokens) {
  assert(dbg != NULL);
  assert(tokens != NULL);

  size_t i = 0;
  const char *cmd = next_token(tokens, &i);

  do {
    if (is_command(cmd, 'c', "continue")) {
      if (!end_of_tokens(tokens, i)) break;
      execmd_continue(dbg);
    } else if (is_command(cmd, 'b', "break")) {
      const char *loc_str = next_token(tokens, &i);
      if (loc_str == NULL) {
	repl_err("Missing location for 'break'");
      } else {
        dbg_addr addr = { 0 };
        if (parse_break_location(*dbg, loc_str, &addr) == SP_OK) {
          if (!end_of_tokens(tokens, i)) {
            break;	    
	  }
          execmd_break(dbg->breakpoints, dbg_to_real(dbg->load_address, addr));
        } else {
	  repl_err("Invalid location for 'break'");
        }
      }
    } else if (is_command(cmd, 'd', "delete")) {
      const char *loc_str = next_token(tokens, &i);
      if (loc_str == NULL) {
	repl_err("Missing location for 'delete'");
      } else {
        dbg_addr addr = { 0 };
        if (parse_break_location(*dbg, loc_str, &addr) == SP_OK) {
          if (!end_of_tokens(tokens, i))
            break;
          execmd_delete(dbg->breakpoints, dbg_to_real(dbg->load_address, addr));
        } else {
	  repl_err("Invalid location for 'delete'");
        }
      }
    }

    else if (is_command(cmd, 'p', "print")) {
      const char *loc_str = next_token(tokens, &i);
      if (loc_str == NULL) {
	repl_err("Missing location to print the value of");
	break;
      }

      PrintFilter filter = PF_NONE;
      const char *delim_str = next_token(tokens, &i);
      if (delim_str != NULL) {
	if (is_filter_delim(delim_str)) {
	  filter = parse_filter(next_token(tokens, &i));
	  if (filter == PF_NONE) {
	    repl_err("Invalid filter");
            break;
	  }
	} else {
	  repl_err("Trailing characters in command");
	  break;
	}
      }


      if (!end_of_tokens(tokens, i)) {
	break;
      }

      real_addr addr = {0};
      if (loc_str[0] == '%') {
	const char *reg_name = loc_str + 1;
	x86_reg reg = 0;
	bool valid_reg = get_register_from_name(reg_name, &reg);
	if (valid_reg) {
	  execmd_print_register(dbg->pid, reg, reg_name, filter);
	} else {
	  repl_err("Invalid register name");
	}
      } else if (is_valid_identifier(loc_str)) {
	warn_register_name_conflict(loc_str);
	execmd_print_variable(dbg, loc_str, filter);
      } else if (parse_base16(loc_str, &addr.value) == SP_OK) {
	execmd_print_memory(dbg->pid, addr, filter);
      } else {
	repl_err("Invalid location to print the value of");
      }
    } else if (is_command(cmd, 't', "set")) {
      const char *loc_str = next_token(tokens, &i);
      if (loc_str == NULL) {
	repl_err("Missing location to set the value of");
	break;
      }

      const char *value_str = next_token(tokens, &i);
      if (value_str == NULL) {
	repl_err("Missing value to set the location to");
	break;
      }

      PrintFilter filter = PF_NONE;
      const char *delim_str = next_token(tokens, &i);
      if (delim_str != NULL) {
	if (is_filter_delim(delim_str)) {
	  filter = parse_filter(next_token(tokens, &i));
	  if (filter == PF_NONE) {
	    repl_err("Invalid filter");
	    break;
	  }

	} else {
	  repl_err("Trailing characters in command");
	  break;
	}
      }

      if (!end_of_tokens(tokens, i)) {
	break;
      }

      /* Parse the input value using the correct base. By default,
	 that same base will be used to print confirmation, too. */
      uint64_t value = 0;
      if (parse_base10(value_str, &value) == SP_OK) {
	filter = default_filter(filter, PF_DEC);
      } else if (parse_base16(value_str, &value) == SP_OK) {
	filter = default_filter(filter, PF_HEX);
      } else {
	repl_err("Invalid value to set the location to");
	break;	
      }

      real_addr addr = {0};
      if (loc_str[0] == '%') {
	const char *reg_name = loc_str + 1;
	x86_reg reg = 0;
	bool valid_reg = get_register_from_name(reg_name, &reg);
	if (valid_reg) {
	  execmd_set_register(dbg->pid, reg, reg_name, value, filter);
	} else {
	  repl_err("Invalid register name");
	}
      } else if (is_valid_identifier(loc_str)) {
	execmd_set_variable(dbg, loc_str, value, filter);
      } else if (parse_base16(loc_str, &addr.value) == SP_OK) {
	execmd_set_memory(dbg->pid, addr, value, filter);
      } else {
	repl_err("Invalid location to set the value of");
      }
    }

    else if (is_command(cmd, 'i', "inst")) {
      if (!end_of_tokens(tokens, i)) break;
      execmd_inst(dbg);
    } else if (is_command(cmd, 'l', "leave")) {
      if (!end_of_tokens(tokens, i)) break;
      execmd_leave(dbg);
    } else if (is_command(cmd, 's', "step")) {
      if (!end_of_tokens(tokens, i)) break;
      execmd_step(dbg);
    } else if (is_command(cmd, 'n', "next")) {
      if (!end_of_tokens(tokens, i)) break;
      execmd_next(dbg);
    } else if (is_command(cmd, 'a', "backtrace")) {
      execmd_backtrace(dbg);
    } else {
      repl_err("Unknown command");
    }
  } while (0); /* Only run this block once. The
     loop is only used to make `break` available
     for  skipping subsequent steps on error. */
}

void handle_debug_command(Debugger *dbg, const char *line) {
  assert(dbg != NULL);
  assert(line != NULL);

  char **tokens = get_command_tokens(line);
  size_t i = 0;
  const char *first_token = next_token(tokens, &i);

  /* Is the command empty? */
  if (first_token == NULL) {
    free_command_tokens(tokens);
    char *last_command = NULL;
    SprayResult res = read_command(dbg->history, &last_command);
    if (res == SP_OK) {
      tokens = get_command_tokens(last_command);
      free(last_command);
    } else {
      repl_err("No command to repeat");
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
  if (is_dyn_exec(dbg->info)) {
    // Open the process' `/proc/<pid>/maps` file.
    char proc_maps_filepath[PROC_MAPS_FILEPATH_LEN];
    snprintf(proc_maps_filepath,
	     PROC_MAPS_FILEPATH_LEN,
	     "/proc/%d/maps",
	     dbg->pid);

    FILE *proc_map = fopen(proc_maps_filepath, "r");
    assert(proc_map != NULL);

    // Read the first address from the file.
    // This is OK since address space
    // layout randomization is disabled.
    char *addr = NULL;
    size_t n = 0;
    ssize_t nread = getdelim(&addr, &n, (int) '-', proc_map);
    fclose(proc_map);
    assert(nread != -1);

    real_addr load_address = { 0 };
    assert(parse_base16(addr, &load_address.value) == SP_OK);

    free(addr);

    // Now update the debugger instance on success.
    dbg->load_address = load_address;
  } else {
    dbg->load_address = (real_addr) { 0 };
  }
}

int setup_debugger(const char *prog_name, char *prog_argv[], Debugger* store) {
  assert(store != NULL);
  assert(prog_name != NULL);

  if (access(prog_name, F_OK) != 0) {
    repl_err("File %s doesn't exist", prog_name);
    return -1;
  }

  DebugInfo *info = init_debug_info(prog_name);
  if (info == NULL) {
    repl_err("Failed to initialize debugging information");
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
    *store = (Debugger){
        .prog_name = prog_name,
        .pid = pid,
        .breakpoints = init_breakpoints(pid),
        .info = info,
        /* `load_address` is initialized by `init_load_address`. */
        .load_address.value = 0,
        .history = init_history(),
    };
    init_load_address(store);
    init_print_source();
  }

  return 0;
}

SprayResult del_debugger(Debugger dbg) {
  free_breakpoints(dbg.breakpoints);
  free_history(dbg.history);
  return free_debug_info(&dbg.info);
}

void run_debugger(Debugger dbg) {
  printf("🐛🐛🐛 %d 🐛🐛🐛\n", dbg.pid);

  const DebugSymbol *main = sym_by_name("main", dbg.info);
  if (main == NULL)
    return;

  dbg_addr start_main = {0};
  if (function_start_addr(main, dbg.info, &start_main) == SP_OK) {
    enable_breakpoint(dbg.breakpoints, dbg_to_real(dbg.load_address, start_main));
    if (continue_execution(&dbg) == SP_ERR)
      return;

    if (wait_for_signal(&dbg) == SP_ERR)
      return;

    disable_breakpoint(dbg.breakpoints, dbg_to_real(dbg.load_address, start_main));
  }

  print_current_source(&dbg);

  char *line_buf = NULL;
  while ((line_buf = linenoise("spray> ")) != NULL) {
    handle_debug_command(&dbg, line_buf);
    linenoiseHistoryAdd(line_buf);
    linenoiseFree(line_buf);
  }
}
