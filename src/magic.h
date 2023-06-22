/* Utils and magic numbers ✨. */

#pragma once

#ifndef _SPRAY_MAGIC_H_
#define _SPRAY_MAGIC_H_

#define unused(x) (void) (x);

enum magic {
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
};

#endif  // _SPRAY_MAGIC_H_
