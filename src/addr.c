#include "addr.h"

dbg_addr real_to_dbg(real_addr offset, real_addr real) {
  return (dbg_addr){real.value - offset.value};
}

real_addr dbg_to_real(real_addr offset, dbg_addr dwarf) {
  return (real_addr){dwarf.value + offset.value};
}

void print_as_addr(uint64_t addr) {
  printf(HEX_FORMAT, addr);
}
