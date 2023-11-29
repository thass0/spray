#include "breakpoints.h"

#include "magic.h"

#include <hashmap.h>
#include <assert.h>
#include <string.h>

typedef struct
{
  real_addr addr;		/* The address is the only member that's
				 * used to compare and look up breakpoints. */
  bool is_enabled;
  uint8_t orig_data;
} Breakpoint;

struct Breakpoints
{
  struct hashmap *map;
  pid_t pid;
};

int
breakpoint_compare (const void *a, const void *b, void *udata)
{
  unused (udata);
  const Breakpoint *breakpoint_a = (Breakpoint *) a;
  const Breakpoint *breakpoint_b = (Breakpoint *) b;
  /* `compare` assumes that strings are used in its implementation.
   * Mimicking `strcmp`, 0 is returned when the keys are equal. */
  return !(breakpoint_a->addr.value == breakpoint_b->addr.value);
}

uint64_t
breakpoint_hash (const void *entry, uint64_t seed0, uint64_t seed1)
{
  const Breakpoint *breakpoint = (Breakpoint *) entry;
  uint64_t addr = breakpoint->addr.value;
  return hashmap_sip (&addr, sizeof (addr), seed0, seed1);
}

Breakpoints *
init_breakpoints (pid_t pid)
{
  struct hashmap *map = hashmap_new (sizeof (Breakpoint), 0, 0, 0,
				     breakpoint_hash, breakpoint_compare,
				     NULL, NULL);
  Breakpoints *breakpoints = (Breakpoints *) calloc (1, sizeof (Breakpoints));
  breakpoints->map = map;
  breakpoints->pid = pid;
  return breakpoints;
}

void
free_breakpoints (Breakpoints *breakpoints)
{
  assert (breakpoints != NULL);
  hashmap_free (breakpoints->map);
  free (breakpoints);
}

bool
lookup_breakpoint (Breakpoints *breakpoints, real_addr address)
{
  assert (breakpoints != NULL);

  Breakpoint lookup = {.addr = address };
  const Breakpoint *check = hashmap_get (breakpoints->map, &lookup);

  /* Did we find an enabled breakpoint? */
  if (check != NULL && check->is_enabled)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/* Wrapper to make internal breakpoint look-ups more comfortable. */
const Breakpoint *
get_breakpoint (Breakpoints *breakpoints, real_addr addr)
{
  assert (breakpoints != NULL);
  return hashmap_get (breakpoints->map, &(Breakpoint) {.addr = addr});
}

SprayResult
enable_breakpoint (Breakpoints *breakpoints, real_addr addr)
{
  assert (breakpoints != NULL);

  const Breakpoint *to_enable = get_breakpoint (breakpoints, addr);

  /* Do we need to create the breakpoint first? */
  if (to_enable == NULL)
    {
      hashmap_set (breakpoints->map, &(Breakpoint) {.addr = addr});
      to_enable = get_breakpoint (breakpoints, addr);
      assert (to_enable != NULL);
    }

  /* Only enable the breakpoint if it's currently disabled.
   * Re-activating an already active breakpoint would delete the
   * original instructions that were overwritten to insert the trap. */
  if (!to_enable->is_enabled)
    {
      /* Read the word at `bp->addr` in the tracee's memory. */
      uint64_t word = { 0 };
      SprayResult res =
	pt_read_memory (breakpoints->pid, to_enable->addr, &word);
      if (res == SP_ERR)
	{
	  return SP_ERR;
	}

      /* Save the original least significant byte. */
      uint64_t orig_data = (uint8_t) (word & BTM_BYTE_MASK);

      /* Set the least significant bytes to the instruction `int 3`.
       * When this instruction is executed, the tracee raises an
       * interrupt and it is sent a trap signal. Receiving this
       * signal stops it. */
      uint64_t int3_data = ((word & ~BTM_BYTE_MASK) | INT3);

      /* Write the trap to the tracee's memory. */
      res = pt_write_memory (breakpoints->pid, to_enable->addr, int3_data);
      if (res == SP_ERR)
	{
	  return SP_ERR;
	}

      /* Update the entry in the hash map. All data belonging to
       * the breakpoint is updated here at once, after the memory write
       * to the tracee's memory has completed successfully. */
      Breakpoint updated = {
	.addr = to_enable->addr,
	.is_enabled = true,
	.orig_data = orig_data,
      };
      hashmap_set (breakpoints->map, &updated);
    }

  return SP_OK;
}

SprayResult
disable_breakpoint (Breakpoints *breakpoints, real_addr addr)
{
  assert (breakpoints != NULL);

  const Breakpoint *to_disable = get_breakpoint (breakpoints, addr);

  if (to_disable != NULL && to_disable->is_enabled)
    {
      /* `ptrace` only operates on whole words, so we need
       * to read what's currently there first, then replace the
       * modified low byte and write it to the address. */

      uint64_t modified_word = 0;
      SprayResult res =
	pt_read_memory (breakpoints->pid, to_disable->addr, &modified_word);
      if (res == SP_ERR)
	{
	  return SP_ERR;
	}

      uint64_t restored_word =
	((modified_word & ~BTM_BYTE_MASK) | to_disable->orig_data);
      res =
	pt_write_memory (breakpoints->pid, to_disable->addr, restored_word);
      if (res == SP_ERR)
	{
	  return SP_ERR;
	}

      /* Update after the write succeeded. */
      Breakpoint disabled = {
	.addr = to_disable->addr,
	.is_enabled = false,
	.orig_data = to_disable->orig_data,
      };
      hashmap_set (breakpoints->map, &disabled);
    }

  return SP_OK;
}
