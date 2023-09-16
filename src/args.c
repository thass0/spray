#include "args.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void print_help_message(const char *me) {
  if (me == NULL) {
    me = "spray";
  }

  fprintf(stderr,
          "usage: %s [-c | --no-color] file [arg1 ...]\n"
	  "\n"
          "  file            the name of the executable file to debug\n"
          "  arg1 ...        arguments passed to the executable to debug\n"
          "  -c, --no-color  disable colored output\n"
          "\n"
          "Spray enters a REPL after launching it, which features the following\n"
	  "commands.\n"
	  "\n"
	  "READING AND WRITING VALUES\n"
	  "\n"
	  "| Command      | Argument(s)          | Description                                             |\n"
	  "|--------------|----------------------|---------------------------------------------------------|\n"
	  "| `print`, `p` | `<variable>`         | Print the value of the runtime variable.                |\n"
	  "|              | `<register>`         | Print the value of the register.                        |\n"
	  "|              | `<address>`          | Print the value of the program's memory at the address. |\n"
	  "| `set`, `t`   | `<variable> <value>` | Set the value of the runtime variable.                  |\n"
	  "|              | `<register> <value>` | Set the value of the register.                          |\n"
	  "|              | `<address> <value>`  | Set the value of the program's memory at the address.   |\n"
	  "\n"
	  "Register names are prefixed with a `%%`, akin to the AT&T\n"
	  "assembly syntax. This avoids name conflicts between register\n"
	  "names and variable names. For example, to read the value of\n"
	  "`rax`, use `print %%rax`. You can find a table of all available\n"
	  "register names in `src/registers.h`.\n"
	  "\n"
	  "Currently, all values are full 64-bit words without any notion\n"
	  "of a type. This will change in the future. A `<value>` can be\n"
	  "a hexadecimal or a decimal number. The default is base 10 and\n"
	  "hexadecimal will only be chosen if the literal contains a\n"
	  "character that's exclusive to base 16 (i.e. one of a - f). You\n"
	  "can prefix the literal with `0x` to explicitly use hexadecimal\n"
	  "in cases where decimal would work as well.\n"
	  "\n"
	  "An `<address>` is always a hexadecimal number. The prefix `0x`\n"
	  "is again optional.\n"
	  "\n"
	  "BREAKPOINTS\n"
	  "\n"
	  "| Command         | Argument(s)     | Description                                   |\n"
	  "|-----------------|-----------------|-----------------------------------------------|\n"
	  "| `break`, `b`    | `<function>`    | Set a breakpoint on the function.             |\n"
	  "|                 | `<file>:<line>` | Set a breakpoint on the line in the file.     |\n"
	  "|                 | `<address>`     | Set a breakpoint on the address.              |\n"
	  "| `delete`, `d`   | `<function>`    | Delete a breakpoint on the function.          |\n"
	  "|                 | `<file>:<line>` | Delete a breakpoint on the line in the file.  |\n"
	  "|                 | `<address>`     | Delete a breakpoint on the address.           |\n"
	  "| `continue`, `c` |                 | Continue execution until the next breakpoint. |\n"
	  "\n"
	  "It's possible that the location passed to `break`, `delete`,\n"
	  "`print`, or `set` is both a valid function name and a valid\n"
	  "hexadecimal address. For example, `add` could refer to a\n"
	  "function called `add` and the number `0xadd`. In such a\n"
	  "case, the default is to interpret the location as a function\n"
	  "name. Use the prefix `0x` to explicitly specify an address.\n"
	  "\n"
	  "STEPPING\n"
	  "\n"
	  "| Command          | Description                                         |\n"
	  "|------------------|-----------------------------------------------------|\n"
	  "| `next`, `n`      | Go to the next line. Don't step into functions.     |\n"
	  "| `step`, `s`      | Go to the next line. Step into functions.           |\n"
	  "| `leave`, `l`     | Step out of the current function.                   |\n"
	  "| `inst`, `i`      | Step to the next instruction.                       |\n"
	  "| `backtrace`, `a` | Print a backtrace starting at the current position. |\n",
          me);
}

const char *prog_name_arg(int argc, char **argv) {
  if (argc > 0 && argv != NULL) {
    return argv[0];
  } else {
    return NULL;
  }
}

/* Parse a flag starting with a single dash. Returns -1 on error. */
int parse_short_flag(const char *flag, Flags *flags) {
  if (flag == NULL || flags == NULL) {
    return -1;
  }

  if (strcmp("-c", flag) == 0) {
    flags->no_color = true;
  } else {
    return -1;
  }

  return 0;
}

/* Parse a flag starting with a double dash. Returns -1 on error. */
int parse_long_flag(const char *flag, Flags *flags) {
  if (flag == NULL || flags == NULL) {
    return -1;
  }

  if (strcmp("--no-color", flag) == 0) {
    flags->no_color = true;
  } else {
    return -1;
  }

  return 0;
}

/* Parse all flags in the command line arguments. Flags start with
 either (1) a single dash followed by a single character or (2) a
 double dash followed by a string. Parsing stops once one of the
 given arguments doesn't fulfill either (1) or (2).
 -1 is returned if the arguments contain invalid flags or the arguments
 to this function are invalid. On success the number of arguments that
 were processed thus far is returned. It is an error if there are no
 arguments left after parsing all flags. */
int parse_flags(int argc, char **argv, Flags *flags) {
  if (flags == NULL || argv == NULL) {
    return -1;
  }

  Flags flags_buf = {0};
  int res = 0;
  int i = 1; /* argv[0] is us. */

  for (; i < argc; i++) {
    if (strncmp(argv[i], "--", 2) == 0) {
      res = parse_long_flag(argv[i], &flags_buf);
    } else if (strncmp(argv[i], "-", 1) == 0) {
      res = parse_short_flag(argv[i], &flags_buf);
    } else {
      /* There are no more flags. */
      break;
    }

    /* Abort on error. */
    if (res == -1) {
      return -1;
    }
  }

  if (i == argc) {
    /* There must be more arguments than just flags. */
    return -1;
  }

  *flags = flags_buf;

  return i;
}

int parse_args(int argc, char **argv, Args *args) {
  if (argv == NULL || args == NULL) {
    return -1;
  }

  int flags_res = parse_flags(argc, argv, &args->flags);
  if (flags_res == -1) {
    return -1;
  } else {
    int file_idx = flags_res;
    args->file = argv[file_idx];

    /* Are there any arguments that should be passed to the debugged executable?
     */
    if (file_idx + 1 < argc) {
      /* The arguments passed to the debugged executable include its name. */
      args->args = argv + file_idx;
      args->n_args = argc - file_idx;
    }
  }

  return 0;
}

static Args GLOBAL_ARGS = {0};

void set_args(const Args *args) {
  assert(args != NULL);

  GLOBAL_ARGS.flags = args->flags;

  /* Replace the filepath to the executable. */
  free(GLOBAL_ARGS.file);
  GLOBAL_ARGS.file = strdup(args->file);

  /* Replace the `args` array. */
  for (size_t i = 0; i < GLOBAL_ARGS.n_args; i++) {
    free(GLOBAL_ARGS.args[i]);
  }
  free(GLOBAL_ARGS.args);

  /* Allocate one pointer more than needed so that the
     array is terminated by a NULL pointer. */
  GLOBAL_ARGS.args = calloc(args->n_args + 1, sizeof(char *));
  for (size_t i = 0; i < args->n_args; i++) {
    GLOBAL_ARGS.args[i] = strdup(args->args[i]);
  }
}

const Args *get_args(void) { return &GLOBAL_ARGS; }
