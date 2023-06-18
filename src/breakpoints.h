#pragma once

#ifndef _SPRAY_BREAKPOINTS_H_
#define _SPRAY_BREAKPOINTS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pid_t pid;
  void *addr;
  bool is_enabled;
  uint8_t orig_data;
} breakpoint;

void enable_breakpoint(breakpoint *bp);
void disable_breakpoint(breakpoint *bp);

#endif // _SPRAY_BREAKPOINTS_H_
