#include "breakpoints.h"
#include "magic.h"

#include <assert.h>
#include <sys/ptrace.h>


// Enable the given breakpoint by replacing the
// instruction at `addr` with `int 3` (0xcc). This
// will make the child raise `SIGTRAP` once the
// instruction is reached.
void enable_breakpoint(Breakpoint *bp) {
  assert(bp != NULL);

  // Read and return a word at `bp->addr` in the tracee's memory.
  uint64_t data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
  // Save the original bottom byte.
  bp->orig_data = (uint8_t) (data & BTM_BYTE_MASK);
  // Set the new bottom byte to `int 3`.
  uint64_t int3_data = ((data & ~BTM_BYTE_MASK) | INT3);
  // Update the word in the tracee's memory.
  ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, int3_data);

  bp->is_enabled = true;
}

// Disable a breakpoint, restoring the original instruction.
void disable_breakpoint(Breakpoint* bp) {
  assert(bp != NULL);

  // `ptrace` only operatores on whole words, so we need
  // to read what's currently there first, then replace the
  // modified low byte and write it to the address.

  uint64_t modified_data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
  uint64_t restored_data = ((modified_data & ~BTM_BYTE_MASK) | bp->orig_data);
  ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, restored_data);

  bp->is_enabled = false;
}

