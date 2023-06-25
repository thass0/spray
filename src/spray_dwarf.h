/* Opinionated wrapper around libdwarf. */

#pragma once

#ifndef _SPRAY_SPRAY_DWARF_H_
#define _SPRAY_SPRAY_DWARF_H_

#include "libdwarf.h"
#include "ptrace.h"

#include <stdbool.h>

Dwarf_Debug dwarf_init(const char *restrict filepath, Dwarf_Error *error);

char *get_function_from_pc(Dwarf_Debug dbg, x86_addr pc);

#endif  // _SPRAY_DWARF_H_
