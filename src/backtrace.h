#pragma once

#ifndef _SPRAY_BACKTRACE_H_
#define _SPRAY_BACKTRACE_H_

#include "info.h"
#include "magic.h"

typedef struct CallFrame CallFrame;

/* Create a call frame starting at the code
   address `pc` and the current stack frame. */
CallFrame *init_backtrace (dbg_addr pc,
			   real_addr load_address,
			   pid_t pid, DebugInfo * info);

/* Print a backtrace starting at the given call frame. */
void print_backtrace (CallFrame * start_frame);

/* Delete the call frame. */
void free_backtrace (CallFrame * call_frame);

#endif /* _SPRAY_BACKTRACE_H_ */
