/*
 * The `ptrace` API is ... special. This header
 * wraps it up for use in the rest of this program.
 * If one of the functions here fails, `errno` will
 * hold the value set by `ptrace`.
 */

#pragma once

#ifndef _SPRAY_PTRACE_H_
#define _SPRAY_PTRACE_H_

#include <stdlib.h>
#include <stdint.h>
#include <sys/user.h>

/* Strong types to separate address and
 * word/data values from each other. */

typedef struct { uint64_t value; } x86_word; 
typedef struct { uint64_t value; } x86_addr;

/* On success all functions below return `PT_OK`.
   Otherwise they return `PT_ERR`. */

typedef enum { PT_OK = 0, PT_ERR = 1} pt_call_result;

pt_call_result pt_read_memory(pid_t pid, x86_addr addr, x86_word *store);
pt_call_result pt_write_memory(pid_t pid, x86_addr addr, x86_word word);

pt_call_result pt_read_registers(pid_t pid, struct user_regs_struct *regs);
pt_call_result pt_write_registers(pid_t pid, struct user_regs_struct *regs);

pt_call_result pt_continue_execution(pid_t pid);
pt_call_result pt_trace_me(void);

#endif // _SPRAY_PTRACE_H_

