#include "breakpoints.h"
#include "magic.h"

#include <assert.h>

// Enable the given breakpoint by replacing the
// instruction at `addr` with `int 3` (0xcc). This
// will make the child raise `SIGTRAP` once the
// instruction is reached.
void enable_breakpoint(Breakpoint *bp) {
  assert(bp != NULL);

  // Read and return a word at `bp->addr` in the tracee's memory.
  x86_word data = { 0 };
  pt_call_result res =
    pt_read_memory(bp->pid, bp->addr, &data);
  unused(res);
  // Save the original bottom byte.
  bp->orig_data = (uint8_t) (data.value & BTM_BYTE_MASK);
  // Set the new bottom byte to `int 3`.
  // When the tracee raises this interrupt, it is sent a
  // SIGTRAP. Receiving this signals stops it.
  x86_word int3_data = { ((data.value & ~BTM_BYTE_MASK) | INT3) };
  // Update the word in the tracee's memory.
  pt_write_memory(bp->pid, bp->addr, int3_data);

  bp->is_enabled = true;
}

// Disable a breakpoint, restoring the original instruction.
void disable_breakpoint(Breakpoint* bp) {
  assert(bp != NULL);

  // `ptrace` only operatores on whole words, so we need
  // to read what's currently there first, then replace the
  // modified low byte and write it to the address.

  x86_word modified_data = { 0 };
  pt_call_result res
    = pt_read_memory(bp->pid, bp->addr, &modified_data);
  unused(res);
  x86_word restored_data = { ((modified_data.value & ~BTM_BYTE_MASK) | bp->orig_data) };
  pt_write_memory(bp->pid, bp->addr, restored_data);

  bp->is_enabled = false;
}

