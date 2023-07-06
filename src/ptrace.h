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
#include <signal.h>
#include <sys/user.h>

#include "magic.h"

/* Strong types to separate address and
 * word/data values from each other. */

typedef struct { uint64_t value; } x86_word; 
typedef struct { uint64_t value; } x86_addr;

SprayResult pt_read_memory(pid_t pid, x86_addr addr, x86_word *store);
SprayResult pt_write_memory(pid_t pid, x86_addr addr, x86_word word);

SprayResult pt_read_registers(pid_t pid, struct user_regs_struct *regs);
SprayResult pt_write_registers(pid_t pid, struct user_regs_struct *regs);

SprayResult pt_continue_execution(pid_t pid);
SprayResult pt_trace_me(void);
SprayResult pt_single_step(pid_t pid);

SprayResult pt_get_signal_info(pid_t pid, siginfo_t *siginfo);

#endif // _SPRAY_PTRACE_H_

