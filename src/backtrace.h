#pragma once

#ifndef _SPRAY_BACKTRACE_H_
#define _SPRAY_BACKTRACE_H_

#include "magic.h"
#include "spray_dwarf.h"
#include "spray_elf.h"

typedef struct {
  x86_addr pc;
  x86_addr frame_pointer;
  int64_t lineno;
  const char *function;
} CallLocation;

typedef struct CallFrame CallFrame;
struct CallFrame {
  CallFrame *caller;
  CallLocation location;
};

CallFrame *init_backtrace(Dwarf_Debug dbg, const ElfFile *elf, pid_t pid,
                          x86_addr pc);
void print_backtrace(CallFrame *start_frame);
void free_backtrace(CallFrame *call_frame);

#endif // _SPRAY_BACKTRACE_H_
