#pragma once

#ifndef _SPRAY_BREAKPOINTS_H_
#define _SPRAY_BREAKPOINTS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pid_t pid;
  uint64_t addr;
  bool is_enabled;
  uint8_t orig_data;
} Breakpoint;

void enable_breakpoint(Breakpoint *bp);
void disable_breakpoint(Breakpoint *bp);

#endif // _SPRAY_BREAKPOINTS_H_
