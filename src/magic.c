#include "magic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    if (strcmp(filter_str, hex_filter) == 0) {
      return PF_HEX;
    } else if (strcmp(filter_str, hex_filter) == 0) {
      return PF_HEX;
    } else if (strcmp(filter_str, bits_filter) == 0) {
      return PF_BITS;
    } else if (strcmp(filter_str, addr_filter) == 0) {
      return PF_ADDR;
    } else if (strcmp(filter_str, dec_filter) == 0) {
      return PF_DEC;
    } else if (strcmp(filter_str, bytes_filter) == 0) {
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

void print_as_addr(uint64_t addr) { print_filtered(addr, PF_ADDR); }

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

#define HEX_FORMAT "0x%lx"
#define DEC_FORMAT "%ld"
#define ADDR_FORMAT "0x%016lx"

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
