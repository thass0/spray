#include "registers.h"
#include "magic.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <stdbool.h>
#include <string.h>

/* Both  `x86_reg` and `reg_descriptors` are laid
 * out the same way as `user_regs_struct` in
 * `/usr/include/sys/user.h`. Hence, `x86_reg` can
 * index both of them.
 */

uint64_t get_register_value(pid_t pid, x86_reg reg) {
  struct user_regs_struct regs;  // Register buffer
  // `addr` is ignored here. `PTRACE_GETREGS` stores all
  // of the tracees general purpose registers in `regs`.
  ptrace(PTRACE_GETREGS, pid, NULL, &regs);

  uint64_t *regs_arr = (uint64_t*) &regs;
  return regs_arr[reg];
}

void set_register_value(pid_t pid, x86_reg reg, uint64_t value) {
  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, pid, NULL, &regs);
  
  // Still unsafe (see `get_register_value`)
  uint64_t *regs_arr = (uint64_t*) &regs;
  regs_arr[reg] = value;

  // Update the register's value.
  ptrace(PTRACE_SETREGS, pid, NULL, &regs);
}

/* Get the register associated with the given DWARF
 * register number by looking it up in `register_descriptors`.
 * Returns whether or not the register number was found.
 * If `true` is returned, `dest` is set to the register.
 * Else, if `dwarf_regnum` wasn't found `dest` remains unchanged.
 */
static inline bool translate_dwarf_regnum(uint8_t dwarf_regnum, x86_reg *dest) {
  assert(dest != NULL);

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
    *dest = reg_descriptors[i].r;
    return true;
  }
}

/* NOTE: All DWARF register numbers are small unsigned integers.
 * Negative values for `dwarf_r` in `reg_descriptors` are used
 * to make those registers  inaccessible via a DWARF register number. */

bool get_dwarf_register_value(pid_t pid, int8_t dwarf_regnum, uint64_t *dest) {
  assert(dest != NULL);
  
  x86_reg associated_reg;

  bool regnum_was_translated =
    translate_dwarf_regnum(dwarf_regnum, &associated_reg);

  if (regnum_was_translated) {
    *dest = get_register_value(pid, associated_reg);
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

bool get_register_from_name(const char *name, x86_reg *dest) {
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
    *dest = reg_descriptors[i].r;
    return true;
  }
}

