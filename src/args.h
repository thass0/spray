/* Command line arguments for spray. */

#pragma once

#ifndef _SPRAY_ARGS_H_
#define _SPRAY_ARGS_H

#include <stdbool.h>

typedef struct {
  bool no_color; /* -c, --no-color */
} Flags;

typedef struct {
  Flags flags;
  char *file;  /* file */
  char **args; /* arg1 ... */
} Args;

/* Parse all command line arguments in `argc` and `argv`. Returns -1 on error.
 */
int parse_args(int argc, char **argv, Args *args);

/* Print the --help message. Defaults to the program name `spray`. */
void print_help_message(const char *me);

/* Get the name of the *this* program from the given command line arguments. */
const char *prog_name_arg(int argc, char **argv);

#endif // _SPRAY_ARGS_H_
