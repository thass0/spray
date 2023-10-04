#include "magic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> /* `getcwd` */
#include <stdlib.h>
#include <assert.h>
#include <limits.h> /* `PATH_MAX` */
#include <stdarg.h>

unsigned n_digits(double num) {
  if (num == 0) {
    return 1;			/* Zero has one digit when written out. */
  } else {
    return ((unsigned) floor(log10(fabs(num)))) + 1;    
  }
}

void indent_by(unsigned n_spaces) {
  for (unsigned i = 0; i < n_spaces; i++) {
    printf(" ");
  }
}

bool str_eq(const char *restrict a, const char *restrict b) {
  return strcmp(a, b) == 0;
}

dbg_addr real_to_dbg(real_addr offset, real_addr real) {
  return (dbg_addr){real.value - offset.value};
}

real_addr dbg_to_real(real_addr offset, dbg_addr dwarf) {
  return (real_addr){dwarf.value + offset.value};
}

PrintFilter parse_filter(const char *filter_str) {
  const char *hex_filter = "hex";
  const char *bits_filter = "bits";
  const char *addr_filter = "addr";
  const char *dec_filter = "dec";
  const char *bytes_filter = "bytes";
  if (filter_str != NULL) {
    if (str_eq(filter_str, hex_filter)) {
      return PF_HEX;
    } else if (str_eq(filter_str, hex_filter)) {
      return PF_HEX;
    } else if (str_eq(filter_str, bits_filter)) {
      return PF_BITS;
    } else if (str_eq(filter_str, addr_filter)) {
      return PF_ADDR;
    } else if (str_eq(filter_str, dec_filter)) {
      return PF_DEC;
    } else if (str_eq(filter_str, bytes_filter)) {
      return PF_BYTES;
    } else {
      return PF_NONE;
    }
  } else {
    return PF_NONE;
  }
}

PrintFilter default_filter(PrintFilter current, PrintFilter _default) {
  if (current == PF_NONE) {
    return _default;
  } else {
    return current;
  }
}

/* Macros for printing binary numbers. https://stackoverflow.com/a/25108449 */
#define PRINTF_BITS_PATTERN_INT8 "%c%c%c%c%c%c%c%c"
#define PRINTF_BITS_INT8(i)		\
  (((i) & 0x80ll) ? '1' : '0'),			\
    (((i) & 0x40ll) ? '1' : '0'),		\
    (((i) & 0x20ll) ? '1' : '0'),		\
    (((i) & 0x10ll) ? '1' : '0'),		\
    (((i) & 0x08ll) ? '1' : '0'),		\
    (((i) & 0x04ll) ? '1' : '0'),		\
    (((i) & 0x02ll) ? '1' : '0'),		\
    (((i) & 0x01ll) ? '1' : '0')

#define PRINTF_BITS_PATTERN_INT16			\
  PRINTF_BITS_PATTERN_INT8 " " PRINTF_BITS_PATTERN_INT8
#define PRINTF_BITS_PATTERN_INT32				\
  PRINTF_BITS_PATTERN_INT16 " " PRINTF_BITS_PATTERN_INT16
#define PRINTF_BITS_PATTERN_INT64				\
  PRINTF_BITS_PATTERN_INT32 " " PRINTF_BITS_PATTERN_INT32

#define PRINTF_BITS_INT16(i)				\
  PRINTF_BITS_INT8((i) >> 8), PRINTF_BITS_INT8(i)
#define PRINTF_BITS_INT32(i)				\
  PRINTF_BITS_INT16((i) >> 16), PRINTF_BITS_INT16(i)
#define PRINTF_BITS_INT64(i)				\
  PRINTF_BITS_INT32((i) >> 32), PRINTF_BITS_INT32(i)

/* Macros for printing bytes made up of two hexadecimal digits each. */
#define PRINTF_BYTES_PATTERN_INT8 "%02hx"
#define PRINTF_BYTES_PATTERN_INT16                                              \
  PRINTF_BYTES_PATTERN_INT8 " " PRINTF_BYTES_PATTERN_INT8
#define PRINTF_BYTES_PATTERN_INT32                                              \
  PRINTF_BYTES_PATTERN_INT16 " " PRINTF_BYTES_PATTERN_INT16
#define PRINTF_BYTES_PATTERN_INT64                                              \
  PRINTF_BYTES_PATTERN_INT32 " " PRINTF_BYTES_PATTERN_INT32
#define PRINTF_BYTES_INT16(i) ((uint8_t)((i) >> 8) & 0xff), ((uint8_t)(i) & 0xff)
#define PRINTF_BYTES_INT32(i) PRINTF_BYTES_INT16((i) >> 16), PRINTF_BYTES_INT16(i)
#define PRINTF_BYTES_INT64(i) PRINTF_BYTES_INT32((i) >> 32), PRINTF_BYTES_INT32(i)

#define HEX_FORMAT "0x%lx"
#define DEC_FORMAT "%ld"

void print_filtered(uint64_t value, PrintFilter filter) {
  switch (filter) {
  case PF_NONE:
  case PF_DEC:
    /* Signed decimal numbers are the default. */
    printf(DEC_FORMAT, (int64_t) value);
    break;
  case PF_HEX:
    printf(HEX_FORMAT, value);
    break;
  case PF_BITS:
    printf(PRINTF_BITS_PATTERN_INT64, PRINTF_BITS_INT64(value));
    break;
  case PF_ADDR:
    printf(ADDR_FORMAT, value);
    break;
  case PF_BYTES:
    printf(PRINTF_BYTES_PATTERN_INT64, PRINTF_BYTES_INT64(value));
    break;
  }
}

// Return the part of `abs_filepath` that's relative to
// the present working directory. It is assumed that 
//
// On success, the pointer that's returned points into
// `abs_filepath`.
//
// Otherwise, `NULL` is returned to signal an error.
const char *relative_filepath(const char *abs_filepath) {
  if (abs_filepath == NULL) {
    return NULL;
  }

  char *cwd_buf = malloc(sizeof(*cwd_buf) * PATH_MAX);
  char *cwd = getcwd(cwd_buf, PATH_MAX);
  if (cwd == NULL) {
    return NULL;
  }

  // Set `i` to the first index in `filepath` that's not part of the cwd.
  size_t i = 0;
  while (cwd[i] == abs_filepath[i]) {
    i++;
  }

  free(cwd_buf);

  if (i == 0) {
    // `abs_filepath` is a relative filepath and should
    // be returned entirely.
    return abs_filepath;
  } else {
    // Return the part of `filepath` that's not part of the cwd.
    // `+ 1` removes the slash character at `abs_filepath[i]`.
    // This slash is left because `cwd` doesn't have a trailing
    // slash character. Hence, this character is the first one
    // where `abs_filepath` and `cmd` differ.
    return abs_filepath + i + 1;
  }
}

void print_as_relative_filepath(const char *filepath) {
  assert(filepath != NULL);

  char *relative_buf = strdup(filepath);
  const char *relative = relative_filepath(relative_buf);
  if (relative != NULL) {
    printf("%s", relative);
    free(relative_buf);
  } else {
    printf("%s", filepath);
  }
}

void print_msg(FILE *stream, const char *kind, const char *fmt, va_list argp) {
  assert(kind != NULL);
  assert(fmt != NULL);
  assert(argp != NULL);

  size_t len = strlen(fmt) + strlen(kind) + 4;
  char *fmt_buf = calloc(len, sizeof(*fmt_buf));
  assert(fmt_buf != NULL);

  size_t n_printed = snprintf(fmt_buf, len, "%s: %s\n", kind, fmt);
  /*
   `snprintf` writes a maximum of `len` bytes, including the
   `\0` byte, and returns the number of bytes written,
   excluding the `\0` byte. Thus, `len` was too small and the
   output was truncated  if `n_printed >= len`.
  */
  assert(n_printed == (len - 1));

  vfprintf(stream, fmt_buf, argp);

  free(fmt_buf);
}

void spray_err(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stderr, "ERR", fmt, argp);
  va_end(argp);
}

void spray_warn(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stderr, "WARN", fmt, argp);
  va_end(argp);
}

void spray_hint(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stderr, "HINT", fmt, argp);
  va_end(argp);
}

void repl_err(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stdout, "ERR", fmt, argp);
  va_end(argp);
}

void repl_warn(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stdout, "WARN", fmt, argp);
  va_end(argp);
}

void repl_hint(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  print_msg(stdout, "HINT", fmt, argp);
  va_end(argp);
}
