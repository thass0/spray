#pragma once

#ifndef _SPRAY_BACKTRACE_H_
#define _SPRAY_BACKTRACE_H_

#include "info.h"
#include "magic.h"

typedef struct CallFrame CallFrame;

// Create a callframe starting at address `pc` and the current frame.
CallFrame *init_backtrace(x86_addr pc, pid_t pid, DebugInfo *info);

// Print a callframe.
void print_backtrace(CallFrame *start_frame);

// Delete a callframe.
void free_backtrace(CallFrame *call_frame);

#endif // _SPRAY_BACKTRACE_H_
