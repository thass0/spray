/*
 * The `ptrace` API is ... special. This header
 * wraps it up for use in the rest of this program.
 * If one of the functions here fails, `errno` will
 * hold the value set by `ptrace`.
 */

#pragma once

#ifndef _SPRAY_PTRACE_H_
#define _SPRAY_PTRACE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <sys/user.h>

#include "magic.h"
#include "addr.h"

SprayResult pt_read_memory(pid_t pid, real_addr addr, uint64_t *read);
SprayResult pt_write_memory(pid_t pid, real_addr addr, uint64_t write);

SprayResult pt_read_registers(pid_t pid, struct user_regs_struct *regs);
SprayResult pt_write_registers(pid_t pid, struct user_regs_struct *regs);

SprayResult pt_continue_execution(pid_t pid);
SprayResult pt_trace_me(void);
SprayResult pt_single_step(pid_t pid);

SprayResult pt_get_signal_info(pid_t pid, siginfo_t *siginfo);

#endif // _SPRAY_PTRACE_H_

