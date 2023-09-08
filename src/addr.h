#pragma once

#ifndef _SPRAY_ADDR_H_
#define _SPRAY_ADDR_H_

#include <stdint.h>		/* `uint64_t` for address values. */
#include <stdio.h>		/* `printf` for `print_addr`. */

typedef struct {
  uint64_t value;
} real_addr;

typedef struct {
  uint64_t value;
} dbg_addr;

/* The runtime addresses in *position independent executables*
   may all be offset by a particular value from the addresses
   which are stored in the binary file itself.
   The addresses found in the DWARF debug information are such
   permanently stored addresses. `dbg_addr` represents them.
   Addresses retrieved from the running process or addresses from
   the debug addresses, which have been offset by the load address,
   are represented by `real_addr`. The are also referred to as *real*
   addresses. */

/* Convert a real address to a debug address. */
dbg_addr real_to_dbg(real_addr offset, real_addr real);

/* Convert a debug address to a real address. */
real_addr dbg_to_real(real_addr offset, dbg_addr dwarf);


/* Don't use this function to print the inner values of the
   address types. Use `print_addr` instead. */
void print_as_addr(uint64_t addr);

/* Print an address. */
#define print_addr(x) print_as_addr(x.value)

/* Format string for addresses that should be printed as hexadecimal numbers. */
#define HEX_FORMAT "0x%016lx"

#endif  // _SPRAY_ADDR_H_
