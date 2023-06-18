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
};

#endif  // _SPRAY_MAGIC_H_
