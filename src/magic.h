/* Utilities, miscellaneous functions and magic numbers.  ✨. */

#pragma once

#ifndef _SPRAY_MAGIC_H_
#define _SPRAY_MAGIC_H_

#include <stdint.h>		/* `uint64_t` for address values. */
#include <stdio.h>		/* `printf` for `print_addr`. */
#include <stdbool.h>

#define unused(x) (void) (x);

enum magic_numbers {
  /* `int 3` instruction code. */
  INT3 = 0xcc,
  /* Mask of lowest byte in number. */
  BTM_BYTE_MASK = 0xff,
  /* Number of registers in the `x86_regs` enum. */
  N_REGISTERS = 27,
  /* Number of characters required to store any possible
   * path `/proc/<pid>/maps`. According to the man-page for
   * proc(5) the maximum pid is up to 2^22. In decimal this
   * number has 7 digits. This plus characters for the rest
   * of the path plus a NULL terminator make up this number. */
  PROC_MAPS_FILEPATH_LEN = 19,
  /* Size of the buffer to print all the tracee's registers.
   * All values are zero-padded so the size is always the same. */
  REGISTER_PRINT_BUF_SIZE = 716,
  /* Width of the format string "\t%8s 0x%016lx" given that the string
   * substituted is no longer that 8 characters. This doesn't
   * include the string's NULL-byte. */
  REGISTER_PRINT_LEN = 26,
  /* Maximum number of instruction-level steps performed by
   * `single_step_line` until giving up trying to find another
   * line. Can be fairly large since the program will likely
   * end after this limit was reached. */
  SINGLE_STEP_SEARCH_LIMIT=128,
};

typedef enum {
  SP_OK,
  SP_ERR,
} SprayResult;

/* Calculate the number of digits in the given number. */
unsigned n_digits(double num);

/* Print n space characters to standard out. */
void indent_by(unsigned n_spaces);

/* Helper to test if two strings are equal (`strcmp(...) == 0`) */
bool str_eq(const char *restrict a, const char *restrict b);

typedef struct {
  uint64_t value;
} real_addr;

typedef struct {
  uint64_t value;
} dbg_addr;

/* The runtime addresses in *position independent executables*
 * may all be offset by a particular value from the addresses
 * which are stored in the binary file itself.
 * The addresses found in the DWARF debug information are such
 * permanently stored addresses. `dbg_addr` represents them.
 * Addresses retrieved from the running process or addresses from
 * the debug addresses, which have been offset by the load address,
 * are represented by `real_addr`. The are also referred to as *real*
 * addresses. */

/* Convert a real address to a debug address. */
dbg_addr real_to_dbg(real_addr offset, real_addr real);

/* Convert a debug address to a real address. */
real_addr dbg_to_real(real_addr offset, dbg_addr dwarf);

/* `printf` format string for addresses. */
#define ADDR_FORMAT "0x%016lx"

/* Filters to format the output. */

typedef enum PrintFilter {
  PF_NONE, /* No filter. */
  PF_HEX,  /* Hexadecimal number. */
  PF_BITS,  /* Binary data. */
  PF_ADDR, /* Address. */
  PF_DEC,  /* Signed decimal number. */
  PF_BYTES, /* Hexadecimal bytes. */
} PrintFilter;

PrintFilter parse_filter(const char *filter_str);

/* Turn `current` into `_default` if `current` is `PF_NONE`. */
PrintFilter default_filter(PrintFilter current, PrintFilter _default);

void print_filtered(uint64_t value, PrintFilter filter);

/* Print `filepath` as relative to the current working directory.
 *
 * `filepath` must not be `NULL`. */
void print_as_relative_filepath(const char *filepath);


/* FORMAT OF MESSAGES
 *
 * 1. They start with a capital letter.
 * 2. They do not include tags like 'ERR' or 'WARN'. Those tags
 *    are added automatically.
 * 3. They do not end with a newline character. Line breaks are
 *    automatically added at the end of each message.
 * 4. They do not end with a period. Periods are only used to delimit
 *    sentences inside the message. If appropriate, question or
 *    exclamation marks may be used at the end of a message.
 * 5. They may use standard `printf` formatting.
 *
 * EXAMPLES OF VALID MESSAGES
 *
 * 'Failed to retrieve data'
 * 'Did you forget to initialize this variable?'
 * 'Variable %s has the value %d'     - Expects a string and an integer.
 *
 * EXAMPLES OF INVALID MESSAGES
 *
 * 'ERR: Cannot open file'            - The 'ERR: ' is added automatically.
 * 'Please provide more information.' - The period at the end is not needed.
 *                                      It only adds visual clutter. */

/* Print messages not tied to the UI to stderr */
void spray_err(const char *msg, ...);
void spray_warn(const char *msg, ...);
void spray_hint(const char *msg, ...);

/* Print messages tied to the debugger REPL to stdout */
void repl_err(const char *msg, ...);
void repl_warn(const char *msg, ...);
void repl_hint(const char *msg, ...);


#endif  /* _SPRAY_MAGIC_H_ */
