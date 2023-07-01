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

bool parse_hex_num(char *restrict str, uint64_t *store) {
  char *str_end = str;
  uint64_t value = strtol(str, &str_end, 16);
  if (str[0] != '\0' && *str_end == '\0') {
    *store = value;
    return true;
  } else {
    return false;
  }
}

static inline void print_addr(x86_addr addr) {
  printf("0x%016lx", addr.value);
}

static inline void print_word(x86_word word) {
  printf("0x%016lx", word.value);
}

/* Get the program counter. */
x86_addr get_pc(pid_t pid) {
  return (x86_addr) { get_register_value(pid, rip).value };
}

/* Get the program counter and remove any offset which is
   added to the physical process counter in PIEs. The DWARF
   debug info doesn't know about this offset. */
x86_addr get_offset_pc(Debugger dbg) {
  x86_addr pc = get_pc(dbg.pid);
  return (x86_addr) { pc.value - dbg.load_address.value };
}

/* Set the program counter. */
void set_pc(pid_t pid, x86_addr pc) {
  set_register_value(pid, rip, (x86_word) { pc.value });
}

void print_current_source(Debugger dbg) {
  x86_addr pc = get_offset_pc(dbg);
  LineEntry line_entry = get_line_entry_from_pc(dbg.dwarf, pc);
  if (line_entry_is_ok(line_entry)) {
    print_source(
      dbg.files,
      line_entry.filepath,
      line_entry.ln,
      3);
  } else {
    printf("<No source info for PC ");
    print_addr(pc);
    printf(">\n");
  }
}

/* Continue execution of child process. */
int continue_execution(pid_t pid) {
  errno = 0;
  pt_continue_execution(pid);
  /* Is the process still alive? */
  if (errno == ESRCH) {
    printf("The process is dead 😭\n");
    return -1;
  }
  return 0;
}

/* Forward declare `wait_for_signal` to allow specific
   signal handlers used by `wait_for_signal` to call
   wait again. */
void wait_for_signal(Debugger dbg);

void handle_sigtrap(Debugger dbg, siginfo_t siginfo) {
  switch (siginfo.si_code) {
    /* Did the tracee hit a breakpoint? */
    case SI_KERNEL:
    case TRAP_BRKPT: {
      /* Go back to real breakpoint address. */
      x86_addr pc = get_pc(dbg.pid);
      set_pc(dbg.pid, (x86_addr) {pc.value - 1});

      printf("Hit breakpoint at address ");
      print_addr(get_pc(dbg.pid));
      printf("\n");

      print_current_source(dbg);
      return;
    }
    /* Did we single step? */
    case TRAP_TRACE:
      return;
    default:
      printf("Unknown SIGTRAP code %d\n", siginfo.si_code);
      return;
  }
}

void handle_sigwinch(Debugger dbg) {
  /* Ignore changes in window size by telling the
     tracee to continue in that case and then wait for
     the next interesting signal. */
  if (continue_execution(dbg.pid) == -1) {
    return;
  }
  wait_for_signal(dbg);
}

void wait_for_signal(Debugger dbg) {
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
    siginfo_t siginfo = { 0 };
    pt_get_signal_info(dbg.pid, &siginfo);
    switch (siginfo.si_signo) {
      case SIGSEGV:
        printf("Child was stopped by a segmentation "
          "fault, reason %d\n", siginfo.si_code);
        break;
      case SIGTRAP:
        handle_sigtrap(dbg, siginfo);
        break;
      case SIGWINCH:
        handle_sigwinch(dbg);
        break;
      default:
        printf("Child was stopped by SIG%s\n",
          sigabbrev_np(WSTOPSIG(wait_status)));
        break;
    }
  }
}

/* Execute the instruction at the breakpoints location
   and stop the tracee again. */
void step_over_breakpoint(Debugger dbg) {
  x86_addr possible_bp_addr = get_pc(dbg.pid);

  if (is_enabled_breakpoint(dbg.breakpoints, possible_bp_addr)) {
    /* Disable the breakpoint, run the original
       instruction and stop. */
    disable_breakpoint(dbg.breakpoints, possible_bp_addr);
    pt_single_step(dbg.pid);
    wait_for_signal(dbg);
    enable_breakpoint(dbg.breakpoints, possible_bp_addr);
  }
}

void single_step_instruction(Debugger dbg) {
  pt_single_step(dbg.pid);
  wait_for_signal(dbg);
}

void single_step_instruction_skip_breakpoints(Debugger dbg) {
  if (lookup_breakpoint(dbg.breakpoints, get_pc(dbg.pid))) {
    step_over_breakpoint(dbg);
  } else {
    single_step_instruction(dbg);
  }
}

void step_out(Debugger dbg) {
  /* If we are on a breakpoint right now, step
     over it first. Otherwise we'll get stuck on it. */
  step_over_breakpoint(dbg);
  /* The return address is stored 8 bytes after the
     start of the stack frame. This is where we want
     to set a breakpoint. */
  x86_addr frame_pointer = (x86_addr) { get_register_value(dbg.pid, rbp).value };
  x86_addr return_address_location = { frame_pointer.value + 8 };
  x86_addr return_address = { 0 };
  pt_read_memory(dbg.pid, return_address_location, (x86_word *) &return_address);

  bool remove_internal_breakpoint = false;
  if (!is_enabled_breakpoint(dbg.breakpoints, return_address)) {
    enable_breakpoint(dbg.breakpoints, return_address);
    remove_internal_breakpoint = true;
  }

  continue_execution(dbg.pid);
  wait_for_signal(dbg);

  if (remove_internal_breakpoint) {
    delete_breakpoint(dbg.breakpoints, return_address);
  }
}

void single_step(Debugger dbg) {
  int init_lineno = get_line_entry_from_pc(dbg.dwarf, get_offset_pc(dbg)).ln;

  while (get_line_entry_from_pc(dbg.dwarf, get_offset_pc(dbg)).ln == init_lineno) {
    single_step_instruction_skip_breakpoints(dbg);
  }
}

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
  pt_call_result res = pt_read_memory(pid, addr, &read);
  unused(res);
  printf(MEM_READ_INDENT);
  print_word(read);
  printf("\n");
}

void exec_command_memory_write(pid_t pid, x86_addr addr, x86_word word) {
  pt_write_memory(pid, addr, word);
  // Print readout of write result:
  x86_word stored = { 0 };
  pt_call_result res = pt_read_memory(pid, addr, &stored);
  unused(res);
  printf(MEM_READ_INDENT);
  print_word(stored);
  printf(" %s\n", WRITE_READ_MSG);
}

void exec_command_register_read(pid_t pid, x86_reg reg, const char *restrict reg_name) {
  x86_word reg_word = get_register_value(pid, reg);
  printf("%8s ", reg_name);
  print_word(reg_word);
  printf("\n");
}

void exec_command_register_write(
  pid_t pid,
  x86_reg reg,
  const char *restrict reg_name,
  x86_word word)
{
  set_register_value(pid, reg, word);
  // Print readout of write result:
  x86_word written = get_register_value(pid, reg);
  printf("%8s ", reg_name);
  print_word(written);
  printf(" %s\n", WRITE_READ_MSG);
}

void exec_command_print(pid_t pid) {
  for (size_t i = 0; i < N_REGISTERS; i++) {
    reg_descriptor desc = reg_descriptors[i];
    x86_word reg_word = get_register_value(pid, desc.r);
    printf("\t%8s ", desc.name);
    print_word(reg_word);
    // Always put two registers on the same line.
    if (i % 2 == 1) { printf("\n"); }
  }
}

void exec_command_break(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);
  enable_breakpoint(breakpoints, addr);
}

/* Execute the instruction at the current breakpoint,
   continue the tracee and wait until it receives the
   next signal. */
void exec_command_continue(Debugger dbg) {
  step_over_breakpoint(dbg);
  if (continue_execution(dbg.pid) == -1) {
    return;
  }
  wait_for_signal(dbg);
}

void exec_command_single_step_instruction(Debugger dbg) {
  single_step_instruction_skip_breakpoints(dbg);
  print_current_source(dbg);
}

void exec_command_step_out(Debugger dbg) {
  step_out(dbg);
}

void exec_command_single_step(Debugger dbg) {
  single_step(dbg);
  print_current_source(dbg);
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
        if (parse_hex_num(addr_str, &addr.value)) {
          exec_command_break(dbg->breakpoints, addr);
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
            if (parse_hex_num(value_str, &word.value)) {
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
        if (parse_hex_num(addr_str, &addr_buf.value)) {
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
          if (parse_hex_num(value_str, &word.value)) {
            exec_command_memory_write(dbg->pid, addr, word);
          } else {
            invalid_error(error_messages[MEMORY_WRITE_VALUE]);
          }
        }
      }
    } else if (is_command(cmd, "i", "istep")) {
      exec_command_single_step_instruction(*dbg);
    } else if (is_command(cmd, "o", "ostep")) {
      exec_command_step_out(*dbg);
    } else if (is_command(cmd, "s", "step")) {
      exec_command_single_step(*dbg);
    } else {
      unknown_cmd_error();
    }
  } while (0); /* Only run this block once. The
   * loop is only used to make `break` available
   * for  skipping subsequent steps on error. */

  free(line);
}

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
    assert(parse_hex_num(addr, &load_address.value) == true);

    free(addr);

    // Now upate the debugger instance on success.
    dbg->load_address = load_address;
  } else {
    dbg->load_address = (x86_addr) { 0 };
  }
}

int setup_debugger(const char *prog_name, Debugger* store) {
  assert(store != NULL);

  // Parse the ELF header.
  ElfFile elf_buf;  /* Must buffer currently because `parse_elf`
                       might change `elf_buf` even on error.
                       This function however should only modify
                       `store` if it's successful. */
  elf_parse_result res = parse_elf(prog_name, &elf_buf);
  if (res != ELF_PARSE_OK) {
    fprintf(stderr, "ELF parse failed: %s",
      elf_parse_result_name(res));
    return -1;
  }

  // Initialized the DWARF info.
  Dwarf_Error error = NULL;
  Dwarf_Debug dwarf = dwarf_init(prog_name, &error);
  if (dwarf == NULL) {
    fprintf(stderr, "DWARF initialization failed: %s",
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
    execl(prog_name, prog_name, NULL);
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
    };
    init_load_address(store);
  }

  return 0;
}

void free_debugger(Debugger dbg) {
  free_source_files(dbg.files);
  free_breakpoints(dbg.breakpoints);
  dwarf_finish(dbg.dwarf);
}

void run_debugger(Debugger dbg) {
  printf("🐛🐛🐛 %d 🐛🐛🐛\n", dbg.pid);

  char *line_buf = NULL;
  while ((line_buf = linenoise("spray> ")) != NULL) {
    handle_debug_command(&dbg, line_buf);
    linenoiseHistoryAdd(line_buf);
    linenoiseFree(line_buf);
  }
  free_debugger(dbg);
}
