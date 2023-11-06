#include "magic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>		/* `getcwd` */
#include <stdlib.h>
#include <assert.h>
#include <limits.h>		/* `PATH_MAX` */
#include <stdarg.h>

unsigned
n_digits (double num)
{
  if (num == 0)
    {
      return 1;			/* Zero has one digit when written out. */
    }
  else
    {
      return ((unsigned) floor (log10 (fabs (num)))) + 1;
    }
}

void
indent_by (unsigned n_spaces)
{
  for (unsigned i = 0; i < n_spaces; i++)
    {
      printf (" ");
    }
}

bool
str_eq (const char *restrict a, const char *restrict b)
{
  return strcmp (a, b) == 0;
}

dbg_addr
real_to_dbg (real_addr offset, real_addr real)
{
  return (dbg_addr)
  {
  real.value - offset.value};
}

real_addr
dbg_to_real (real_addr offset, dbg_addr dwarf)
{
  return (real_addr)
  {
  dwarf.value + offset.value};
}

FormatFilter
parse_format (const char *str)
{
  if (str != NULL)
    {
      if (str_eq (str, "hex"))
	{
	  return FMT_HEX;
	}
      else if (str_eq (str, "bits"))
	{
	  return FMT_BITS;
	}
      else if (str_eq (str, "addr"))
	{
	  return FMT_ADDR;
	}
      else if (str_eq (str, "dec"))
	{
	  return FMT_DEC;
	}
      else if (str_eq (str, "bytes"))
	{
	  return FMT_BYTES;
	}
      else
	{
	  return FMT_NONE;
	}
    }
  else
    {
      return FMT_NONE;
    }
}

FormatFilter
default_format (FormatFilter current, FormatFilter _default)
{
  if (current == FMT_NONE)
    {
      return _default;
    }
  else
    {
      return current;
    }
}

/* Macros for printing binary numbers. https://stackoverflow.com/a/25108449 */
#define PRINTF_BITS_PATTERN_INT8 "%c%c%c%c%c%c%c%c"
#define PRINTF_BITS_INT8(i)			\
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
#define PRINTF_BYTES_PATTERN_INT16				\
  PRINTF_BYTES_PATTERN_INT8 " " PRINTF_BYTES_PATTERN_INT8
#define PRINTF_BYTES_PATTERN_INT32				\
  PRINTF_BYTES_PATTERN_INT16 " " PRINTF_BYTES_PATTERN_INT16
#define PRINTF_BYTES_PATTERN_INT64				\
  PRINTF_BYTES_PATTERN_INT32 " " PRINTF_BYTES_PATTERN_INT32
#define PRINTF_BYTES_INT16(i) ((uint8_t)((i) >> 8) & 0xff), ((uint8_t)(i) & 0xff)
#define PRINTF_BYTES_INT32(i) PRINTF_BYTES_INT16((i) >> 16), PRINTF_BYTES_INT16(i)
#define PRINTF_BYTES_INT64(i) PRINTF_BYTES_INT32((i) >> 32), PRINTF_BYTES_INT32(i)

#define HEX_FORMAT "0x%lx"
#define DEC_FORMAT "%ld"

char *
print_format (uint64_t value, FormatFilter filter)
{
  /* A 512 byte maximum means at most 8 characters per bit.
   * That should be sufficient (+1 for '\0') */
  int n = 513;
  char *buf = malloc (n);
  assert (buf != NULL);
  
  switch (filter)
    {
    case FMT_NONE:
    case FMT_DEC:
      /* Signed decimal numbers are the default. */
      snprintf (buf, n, DEC_FORMAT, (int64_t) value);
      break;
    case FMT_HEX:
      snprintf (buf, n, HEX_FORMAT, value);
      break;
    case FMT_BITS:
      snprintf (buf, n, PRINTF_BITS_PATTERN_INT64,
		PRINTF_BITS_INT64 (value));
      break;
    case FMT_ADDR:
      snprintf (buf, n, ADDR_FORMAT, value);
      break;
    case FMT_BYTES:
      snprintf (buf, n, PRINTF_BYTES_PATTERN_INT64,
		PRINTF_BYTES_INT64 (value));
      break;
    }

  return buf;
}

const char *
relative_filepath (const char *abs_filepath)
{
  if (abs_filepath == NULL)
    {
      return NULL;
    }

  char *cwd_buf = malloc (sizeof (*cwd_buf) * PATH_MAX);
  char *cwd = getcwd (cwd_buf, PATH_MAX);
  if (cwd == NULL)
    {
      return NULL;
    }

  /* Set `i` to the first index in `filepath` that's not part of the cwd. */
  size_t i = 0;
  while (cwd[i] == abs_filepath[i])
    {
      i++;
    }

  free (cwd_buf);

  if (i == 0)
    {
      /* `abs_filepath` is a relative filepath and
       * should be returned entirely. */
      return abs_filepath;
    }
  else
    {
      /* Return the part of `filepath` that's not part of the cwd.
       * `+ 1` removes the slash character at `abs_filepath[i]`.
       * This slash is left because `cwd` doesn't have a trailing
       * slash character. Hence, this character is the first one
       * where `abs_filepath` and `cmd` differ. */
      return abs_filepath + i + 1;
    }
}

void
print_as_relative_filepath (const char *filepath)
{
  assert (filepath != NULL);

  char *relative_buf = strdup (filepath);
  const char *relative = relative_filepath (relative_buf);
  if (relative != NULL)
    {
      printf ("%s", relative);
      free (relative_buf);
    }
  else
    {
      printf ("%s", filepath);
    }
}

void
print_msg (FILE *stream, const char *kind, const char *fmt, va_list argp)
{
  assert (kind != NULL);
  assert (fmt != NULL);
  assert (argp != NULL);

  size_t len = strlen (fmt) + strlen (kind) + 4;
  char *fmt_buf = calloc (len, sizeof (*fmt_buf));
  assert (fmt_buf != NULL);

  size_t n_printed = snprintf (fmt_buf, len, "%s: %s\n", kind, fmt);
  /* `snprintf` writes a maximum of `len` bytes, including the
   * `\0` byte, and returns the number of bytes written,
   * excluding the `\0` byte. Thus, `len` was too small and the
   * output was truncated  if `n_printed >= len`. */
  assert (n_printed == (len - 1));

  vfprintf (stream, fmt_buf, argp);

  free (fmt_buf);
}

void
spray_err (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stderr, "ERR", fmt, argp);
  va_end (argp);
}

void
spray_warn (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stderr, "WARN", fmt, argp);
  va_end (argp);
}

void
spray_hint (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stderr, "HINT", fmt, argp);
  va_end (argp);
}

void
repl_err (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stdout, "ERR", fmt, argp);
  va_end (argp);
}

void
repl_warn (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stdout, "WARN", fmt, argp);
  va_end (argp);
}

void
repl_hint (const char *fmt, ...)
{
  va_list argp;
  va_start (argp, fmt);
  print_msg (stdout, "HINT", fmt, argp);
  va_end (argp);
}
