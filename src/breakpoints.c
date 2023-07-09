#include "breakpoints.h"

#include "magic.h"
#include "../dependencies/hashmap.c/hashmap.h"

#include <assert.h>
#include <string.h>

typedef struct {
  x86_addr addr;
  bool is_enabled;
  uint8_t orig_data;
} Breakpoint;

struct Breakpoints {
  struct hashmap *map;
  pid_t pid;
};

int breakpoint_compare(const void *a, const void *b, void *udata) {
  unused(udata);
  const Breakpoint *breakpoint_a = (Breakpoint *) a;
  const Breakpoint *breakpoint_b = (Breakpoint *) b;
  /* `compare` assumes that strings are used. Just like
     `strcmp` we return 0 to signal equality. */
  return !(breakpoint_a->addr.value == breakpoint_b->addr.value);
}

uint64_t breakpoint_hash(const void *entry, uint64_t seed0, uint64_t seed1) {
  const Breakpoint *breakpoint = (Breakpoint *) entry;
  uint64_t addr = breakpoint->addr.value;
  return hashmap_sip(&addr, sizeof(addr), seed0, seed1);
}

Breakpoints *init_breakpoints(pid_t pid) {
  struct hashmap *map = hashmap_new(sizeof(Breakpoint), 0, 0, 0,
    breakpoint_hash, breakpoint_compare, NULL, NULL);
  Breakpoints *breakpoints = (Breakpoints *) calloc (1, sizeof(Breakpoints));
  breakpoints->map = map;
  breakpoints->pid = pid;
  return breakpoints;
}

void free_breakpoints(Breakpoints *breakpoints) {
  assert(breakpoints != NULL);
  hashmap_free(breakpoints->map);
  free(breakpoints);
}

bool lookup_breakpoint(Breakpoints *breakpoints, x86_addr address) {
  assert(breakpoints != NULL);

  /* The only paramter that is relevant for the lookup
     is the address. */
  Breakpoint lookup = { .addr=address };
  Breakpoint *check = (Breakpoint *) hashmap_get(breakpoints->map, &lookup);

  /* Did we find an enabled breakpoint? */
  if (check != NULL && check->is_enabled) {
    return true;
  } else {
    return false;
  }
}

void enable_breakpoint(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);

  Breakpoint lookup = {
    .addr=addr,
    /* Set default values in case we need to create a
       new breakpoints. These don't matter for the lookup. */
    .is_enabled=false,
    .orig_data=0,
  };

  Breakpoint *enable = (Breakpoint *) hashmap_get(breakpoints->map, &lookup);

  /* Do we need to create the breakpoint first? */
  if (enable == NULL) {
    hashmap_set(breakpoints->map, &lookup);
    enable = (Breakpoint *) hashmap_get(breakpoints->map, &lookup);
    assert(enable != NULL);
  }

  /* Do we need to enable the breakpoint? We wouldn't want to
     execute the following code if the breakpoint were already
     enabled. If we did, we would loose the original data. */
  if (!enable->is_enabled) {
    // Read and return a word at `bp->addr` in the tracee's memory.
    x86_word data = { 0 };
    SprayResult res =
      pt_read_memory(breakpoints->pid, enable->addr, &data);
    unused(res);
    // Save the original bottom byte.
    enable->orig_data = (uint8_t) (data.value & BTM_BYTE_MASK);
    // Set the new bottom byte to `int 3`.
    // When the tracee raises this interrupt, it is sent a
    // SIGTRAP. Receiving this signals stops it.
    x86_word int3_data = { ((data.value & ~BTM_BYTE_MASK) | INT3) };
    // Update the word in the tracee's memory.
    pt_write_memory(breakpoints->pid, enable->addr, int3_data);

    enable->is_enabled = true;
  }
}

void disable_breakpoint(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);

  Breakpoint lookup = { .addr=addr };
  Breakpoint *disable = (Breakpoint *) hashmap_get(breakpoints->map, &lookup);

  if (disable != NULL && disable->is_enabled) {
    // `ptrace` only operatores on whole words, so we need
    // to read what's currently there first, then replace the
    // modified low byte and write it to the address.

    x86_word modified_data = { 0 };
    SprayResult res
      = pt_read_memory(breakpoints->pid, disable->addr, &modified_data);
    assert(res == SP_OK);
    x86_word restored_data = { ((modified_data.value & ~BTM_BYTE_MASK) | disable->orig_data) };
    pt_write_memory(breakpoints->pid, disable->addr, restored_data);

    disable->is_enabled = false; 
  }
}

void delete_breakpoint(Breakpoints *breakpoints, x86_addr addr) {
  assert(breakpoints != NULL);

  disable_breakpoint(breakpoints, addr);
  Breakpoint lookup = { .addr=addr };
  hashmap_delete(breakpoints->map, &lookup);
}
