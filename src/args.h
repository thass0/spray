/* Command line arguments for spray. */

#pragma once

#ifndef _SPRAY_ARGS_H_
#define _SPRAY_ARGS_H_

#include <stdbool.h>
#include <stdlib.h>

typedef struct {
  bool no_color; /* -c, --no-color */
} Flags;

typedef struct {
  Flags flags;
  char *file;  /* file */
  char **args; /* arg1 ... */
  size_t n_args;
} Args;

/* Get a pointer to the arguments set using `set_args`. The return
   values are meaningful only after `set_args` was called once. */
const Args *get_args(void);

/* If `SET_ARGS_ONCE` is defined, extra utilities are declared that
   allow retrieving and the storing the command line arguments. */
#ifdef SET_ARGS_ONCE

/* Parse all command line arguments in `argc` and `argv`. Returns -1 on error.
   Pointers to data in `argv` are stored in `args`.*/
int parse_args(int argc, char **argv, Args *args);

/* Print the --help message. Defaults to the program name `spray`. */
void print_help_message(const char *me);

/* Get the name of the *this* program from the given command line arguments.
   Can be used to get the program name for `print_help_message`. */
const char *prog_name_arg(int argc, char **argv);

/* Copy the given arguments so that they can be accessed from
   anywhere in the program. Don't call this function if any
   pointers returned by `get_args` are still alive. In general
   it's best to call this function only once right after parsing
   the arguments and then never again. */
void set_args(const Args *args);

#endif // SET_ARGS_ONCE

#endif // _SPRAY_ARGS_H_
