/* Utilities, miscellaneous functions and magic numbers.  ✨. */

#pragma once

#ifndef _SPRAY_MAGIC_H_
#define _SPRAY_MAGIC_H_

#define unused(x) (void) (x);

enum magic_numbers {
  // `int 3` instruction code.
  INT3 = 0xcc,
  // Mask of lowest byte in number.
  BTM_BYTE_MASK = 0xff,
  // Number of registers in the `x86_regs` enum.
  N_REGISTERS = 27,
  // Number of characters required to store any possible
  // path `/proc/<pid>/maps`. According to the man-page for
  // proc(5) the maximum pid is up to 2^22. In decimal this
  // number has 7 digits. This plus characters for the rest
  // of the path plus a NULL terminator make up this number.
  PROC_MAPS_FILEPATH_LEN = 19,
  // Size of the buffer to print all the tracee's registers.
  // All values are zero-padded so the size is always the same.
  REGISTER_PRINT_BUF_SIZE = 716,
  // Width of the format string "\t%8s 0x%016lx" given that the string
  // substituted is no longer that 8 characters. This doesn't
  // include the string's NULL-byte.
  REGISTER_PRINT_LEN = 26,
  // Maximum number of instruction-level steps performed by
  // `single_step_line` until giving up trying to find another
  // line. Can be fairly large since the program will likely
  // end after this limit was reached.
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

#endif  // _SPRAY_MAGIC_H_
