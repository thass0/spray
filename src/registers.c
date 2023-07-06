#include "registers.h"
#include "magic.h"
#include "ptrace.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

/* Both  `x86_reg` and `reg_descriptors` are laid
   out the same way as `user_regs_struct` in
   `/usr/include/sys/user.h`. Hence, `x86_reg` can
   index both of them. */

SprayResult get_register_value(pid_t pid, x86_reg reg, x86_word *store) {
  assert(store != NULL);

  struct user_regs_struct regs;  // Register buffer
  SprayResult res = pt_read_registers(pid, &regs);
  if (res == SP_ERR) {
    return SP_ERR;
  } else {
    uint64_t *regs_as_array = (uint64_t*) &regs;
    *store = (x86_word) { .value=regs_as_array[reg] };
    return SP_OK;
  }
}

SprayResult set_register_value(pid_t pid, x86_reg reg, x86_word word) {
  struct user_regs_struct regs;
  SprayResult res = pt_read_registers(pid, &regs);
  if (res ==  SP_ERR) {
    return SP_ERR;
  } else { 
    uint64_t *regs_as_array = (uint64_t*) &regs;
    regs_as_array[reg] = word.value;
    return pt_write_registers(pid, &regs);
  }
}

/* Get the register associated with the given DWARF
 * register number by looking it up in `register_descriptors`.
 * Returns whether or not the register number was found.
 * If `true` is returned, `store` is set to the register.
 * Else, if `dwarf_regnum` wasn't found `store` remains unchanged.
 */
static inline bool translate_dwarf_regnum(uint8_t dwarf_regnum, x86_reg *store) {
  assert(store != NULL);

  size_t i  = 0;
  for ( ; i < N_REGISTERS; i++) {
    if (reg_descriptors[i].dwarf_r == (int) dwarf_regnum) {
      break;
    }
  }

  if (i == N_REGISTERS) {
    /* We searched the entire array
     * without finding a match. : (
     */
    return false;
  } else {
    *store = reg_descriptors[i].r;
    return true;
  }
}

/* NOTE: All DWARF register numbers are small unsigned integers.
 * Negative values for `dwarf_r` in `reg_descriptors` are used
 * to make those registers  inaccessible via a DWARF register number. */

bool get_dwarf_register_value(pid_t pid, int8_t dwarf_regnum, x86_word *store) {
  assert(store != NULL);
  
  x86_reg associated_reg;

  bool regnum_was_translated =
    translate_dwarf_regnum(dwarf_regnum, &associated_reg);

  if (regnum_was_translated) {
    SprayResult res = get_register_value(pid, associated_reg, store);
    assert(res == SP_OK);
    return true;
  } else {
    return false;
  }
}

const char *get_name_from_register(x86_reg reg) {
  size_t i = 0;
  for ( ; i < N_REGISTERS; i++) {
    if (reg_descriptors[i].r == reg) {
      break;
    }
  }

  // `reg_descriptors` maps all possible values
  // of `reg`. Therefore the name *must* be found.
  assert(i != N_REGISTERS);

  return reg_descriptors[i].name;
}

bool get_register_from_name(const char *name, x86_reg *store) {
  size_t i = 0;
  for ( ; i < N_REGISTERS; i++) {
    if (strcmp(reg_descriptors[i].name, name) == 0) {
      break;
    }
  }

  if (i == N_REGISTERS) {
    /* Couldn't find a register named `name`. */
    return false;
  } else {
    *store = reg_descriptors[i].r;
    return true;
  }
}

