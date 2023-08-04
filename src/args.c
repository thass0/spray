#include "args.h"

#include <stdio.h>
#include <string.h>

void print_help_message(const char *me) {
  if (me == NULL) {
    me = "spray";
  }

  fprintf(stderr,
          "usage: %s [-c | --no-color] file [arg1 ...]\n"
          "  file            the name of the executable file to debug\n"
          "  arg1 ...        arguments passed to the executable to debug\n"
          "  -c, --no-color  disable colored output\n",
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
    }
  }

  return 0;
}
