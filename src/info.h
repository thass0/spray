/* Information about the executable that's being debugged. */

#pragma once

#ifndef _SPRAY_INFO_H_
#define _SPRAY_INFO_H_

#include "breakpoints.h"
#include "registers.h"
#include <stdbool.h>

typedef struct DebugInfo DebugInfo;

// Initialize debugging information. Returns NULL on error.
DebugInfo *init_debug_info(const char *filepath);

// Free the given `DebugInfo` instance. Any pointer
// to an object returned from a function in this file
// becomes invalid if the `DebugInfo` instance given
// to that function is deleted.
// Returns `SP_ERR` if some resources couldn't be deleted.
SprayResult free_debug_info(DebugInfo **infop);

// A symbol in the executable that's being debugged.
typedef struct DebugSymbol DebugSymbol;

// Get a debug symbol by its name. Returns NULL on error.
const DebugSymbol *sym_by_name(const char *name, DebugInfo *info);

// Get a debug symbol by an address that belongs to it. Returns NULL on error.
const DebugSymbol *sym_by_addr(dbg_addr addr, DebugInfo *info);

// Get the name of the given symbol. Returns NULL if there is no name.
const char *sym_name(const DebugSymbol *sym, const DebugInfo *info);

// Get the address at which the code of the first line
// of the given function starts. Returns `SP_ERR` and
// leaves `addr` untouched if the symbol doesn't refer
// to a function.
SprayResult function_start_addr(const DebugSymbol *func,
				const DebugInfo *info,
                                dbg_addr *addr);

// Get the start address (low PC) of the given symbol.
dbg_addr sym_start_addr(const DebugSymbol *sym);

// Get the end address (high PC) of the given symbol.
dbg_addr sym_end_addr(const DebugSymbol *sym);

// Get the address of the given symbol. Returns the same address
// as `sym_start_addr` if the symbol was created from a name.
dbg_addr sym_addr(const DebugSymbol *sym);

// Get the filepath of the source file that belongs to the symbol.
// The string that's returned is owned and later deleted by `info`.
const char *sym_filepath(const DebugSymbol *sym, const DebugInfo *info);

// A position in a source file.
typedef struct Position {
  uint32_t line;
  uint32_t column;
  // `true` if this position perfectly matches the symbol used to
  // retrieve it. Otherwise this position only represents the closest
  // location to describe the symbol with.
  bool is_exact;
} Position;

// Returns the position of the symbol in the source file
// that belongs to the symbol. NULL is returned if no
// such position could be retrieved.
const Position *sym_position(const DebugSymbol *sym, const DebugInfo *info);

// Return the position that belongs to the given address.
// Returns NULL on error.
const Position *addr_position(dbg_addr addr, DebugInfo *info);

// Returns the function name that belongs to the given address.
// Returns NULL on error.
const char *addr_name(dbg_addr addr, DebugInfo *info);

// Returns the filepath that belongs to the given address.
// Returns NULL on error.
const char *addr_filepath(dbg_addr addr, DebugInfo *info);

/* The following function don't fit the regular scheme of
   this interface. They are currently required by might
   be incorporated in a generic interface later. */

// Returns the address that belongs to the given filepath and line number.
// `SP_ERR` is returned if no such address could be found and `addr`
// stays untouched.
SprayResult addr_at(const char *filepath,
		    uint32_t lineno,
                    const DebugInfo *info,
		    dbg_addr *addr);

// Is this a dynamic executable which is relocated?
bool is_dyn_exec(const DebugInfo *info);

// Set breakpoints required to step over the line referred to by `func`.
// On error `SP_ERR` is returned and nothing has to be deleted.
SprayResult set_step_over_breakpoints(const DebugSymbol *func,
                                      const DebugInfo *info,
                                      real_addr load_address,
                                      Breakpoints *breakpoints,
                                      real_addr **to_del, size_t *n_to_del);

// Location of a variable.
//
// This includes a description of where to find this variable
// in the memory of the running debugee process, the path to
// the file where the variable is declared and line number in
// this file.
//
// It does not include the name the variable is declared as.
// The name should be easily accessible since it's required
// to create an instance of `VarLocation`.
typedef struct VarLocation VarLocation;

/* Return a pointer to the address of the location or `NULL`
   if the location doesn't represent an address. */
const real_addr *var_loc_addr(VarLocation *loc);

/* Return a pointer to the register location stands for
   or `NULL` if the location doesn't represent a register. */
const x86_reg *var_loc_reg(VarLocation *loc);

/* Check the type of a location. */
bool is_addr_loc(VarLocation *loc);
bool is_reg_loc(VarLocation *loc);

// Return the path of the file and the line number in the file
// where the variable described by `loc` was declared.
//
// Both of them are optional. `0` indicates that there is no
// line number (since line numbers start at 1!), and `NULL` is
// returned if there is no path.
const char *var_loc_path(VarLocation *loc);
unsigned var_loc_line(VarLocation *loc);

// Print the path and the line of the given variable.
//
// This function uses the values as `var_loc_path` and
// `var_loc_line` return.
//
// `loc` must not be `NULL`.
void print_var_loc(VarLocation *loc);

// Get the location of the variable with the
// given name in the scope around `pc`.
//
// On success, a new heap-allocated location is returned.
// This location must be manually `free`'d (TODO: make
// happen automatically when `info` is destroyed).
//
// `NULL` is returned on error.
VarLocation *init_var_loc(dbg_addr pc,
			 real_addr load_address,
			 const char *var_name,
			 pid_t pid,
			 const DebugInfo *info);

// Delete a `VarLocation` pointer as returned by `init_var_loc`.
void free_var_loc(VarLocation *loc);

#endif // _SPRAY_INFO_H_
