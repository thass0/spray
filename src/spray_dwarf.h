/* Spray's wrapper around libdwarf. */

#pragma once

#ifndef _SPRAY_SPRAY_DWARF_H_
#define _SPRAY_SPRAY_DWARF_H_

#include "ptrace.h"
#include "spray_elf.h" /* `ElfFile` in `SdLocEvalCtx` */
#include "registers.h" /* `x86_reg` in `SdLocation` */

#include <libdwarf-0/libdwarf.h>
#include <stdbool.h>

/* Initialized debug info. Returns NULL on error. */
Dwarf_Debug sd_dwarf_init(const char *filepath, Dwarf_Error *error);

/* Get the filepath of the file while the given PC is part of.
   The strings that's returned must be free' by the caller. */
char *sd_filepath_from_pc(Dwarf_Debug dbg, dbg_addr pc);

typedef struct {
  bool is_ok;
  bool new_statement;
  bool prologue_end;
  // Set to true if the PC used to retrieve the
  // line entry was exactly equal to `addr`.
  bool is_exact;
  unsigned ln;
  unsigned cl;
  dbg_addr addr;
  // Don't free this string. It's owned by the `Dwarf_Debug` instance.
  char *filepath;
} LineEntry;

// Returns the line entry for th PC if the line entry contains
// the address of PC. On error `is_ok` is set to false.
LineEntry sd_line_entry_from_pc(Dwarf_Debug dbg, dbg_addr pc);

// Get the line entry at the given position.
LineEntry sd_line_entry_at(Dwarf_Debug dbg, const char *filepath,
                           unsigned lineno);

typedef SprayResult (*LineCallback)(LineEntry *line, void *const data);

/* Call `callback` for each new statement line entry
   in the subprogram with the given name. */
SprayResult sd_for_each_line_in_subprog(Dwarf_Debug dbg, const char *fn_name,
                                        const char *filepath,
                                        LineCallback callback,
                                        void *const init_data);

/* Figure out where the function prologue of the function starting
   at `low_pc` ends and return this address. Used for breakpoints on
   functions to break only after the prologue.
   `prologue_start` is the same address as a subprogram's low PC
   and `function_end` is the same address as the high PC. */
SprayResult sd_effective_start_addr(Dwarf_Debug dbg,
				    dbg_addr prologue_start,
                                    dbg_addr function_end,
                                    dbg_addr *function_start);


/* Locations of variables */

typedef struct SdExpression SdLocdesc;
typedef struct SdLocRange SdLocRange;

/* A DWARF loclist (list of DWARF expressions) used to
   describe the locations of variables over time */
typedef struct SdLoclist {
  size_t n_exprs;
  SdLocdesc *exprs;
  SdLocRange *ranges;
} SdLoclist;

/* A DWARF DIE attribute that can be used in combination
   with `sd_init_loclist` to initialize a new location list.

   `sd_init_loc_attr` is the only way to initialize this struct,
   because this function ensures that the form bounds are met. */
typedef struct SdLocAttr {
  Dwarf_Attribute attr;
} SdLocAttr;

SprayResult sd_init_loc_attr(Dwarf_Debug dbg,
			     Dwarf_Die die,
			     Dwarf_Attribute attr,
			     SdLocAttr *attr_dest);

/* Get the location description attribute of the variable
   with the given name. `pc` is used to choose the closest
   variable if the variable name occurs more than once. */
SprayResult sd_location_from_variable_name(Dwarf_Debug dbg,
					   dbg_addr pc,
					   const char *var_name,
					   SdLocAttr *attr);
  
/* Initialize a location list from the location description attribute in `loc_attr`. */
SprayResult sd_init_loclist(Dwarf_Debug dbg,
			    SdLocAttr loc_attr,
                            SdLoclist *loclist);

/* Delete the given location list. */
void del_loclist(SdLoclist *loclist);

/* Print the given location list. */
void print_loclist(SdLoclist loclist);

/* Contextual information used to evaluate
   certain operations in location descriptions. */
typedef struct SdLocEvalCtx {
  pid_t pid;
  dbg_addr pc;
  const ElfFile *elf;
  real_addr load_address;
} SdLocEvalCtx;

/* Location that's the result of evaluating a location list */
typedef struct SdLocation {
  enum {
    LOC_ADDR,
    LOC_REG,
  } tag;
  union {
    real_addr addr;
    x86_reg reg;
  };
  /* ... the pain in my heart of not being able to use tagged-enums in C. */
} SdLocation;

/* Create an address instance of `SdLocation`. */
SdLocation sd_loc_addr(real_addr addr);
SdLocation sd_loc_as_addr(uint64_t addr);

/* Create a register instance of `SdLocation`. */
SdLocation sd_loc_reg(x86_reg reg);

/* Evaluate the given location list and return the
   location that it describes currently. */
SprayResult sd_eval_loclist(Dwarf_Debug dbg,
			    SdLocEvalCtx ctx,
			    SdLoclist loclist,
			    SdLocation *location);

#ifdef UNIT_TESTS

/* Expose some of the internal interfaces. */

/* Search callback types for searching DIEs. */

typedef struct SearchFor {
  unsigned level;   /* Level in the DIE tree. */
  const void *data; /* Custom data used as context while searching. */
} SearchFor;

typedef struct SearchFindings {
  void *data;			/* Custom data collected while searching */
} SearchFindings;

typedef bool (*SearchCallback)(Dwarf_Debug, Dwarf_Die, SearchFor, SearchFindings);

/* Search function that searches DIEs for different content. */
int sd_search_dwarf_dbg(Dwarf_Debug dbg,
			Dwarf_Error *const error,
			SearchCallback search_callback,
			const void *search_for_data,
			void *search_findings_data);

/* Find a `DW_TAG_subprogram` DIE by its name. */
bool sd_is_subprog_with_name(Dwarf_Debug dbg, Dwarf_Die die, const char *name);

/* Describe a result returned by libdwarf. */
const char *what_dwarf_result(int dwarf_res);

/* Full definition of types internal to `SdLoclist`. */
typedef struct SdLocRange {
  bool meaningful;
  real_addr lowpc;		/* Inclusive lower bound. */
  real_addr highpc;		/* Exclusive upper bound. */
} SdLocRange;

typedef Dwarf_Small SdOperator;
typedef Dwarf_Unsigned SdOperand;

/* A single operation in a DWARF expression. */
typedef struct SdOperation {
  SdOperator opcode;
  /* The operands 1-3 can be addressed either as single
     struct members or as elements in an array. */
  union {
    struct {
      SdOperand operand1;
      SdOperand operand2;
      SdOperand operand3;
    };
    SdOperand operands[3];
  };
} SdOperation;

/* A DWARF expression used for locexprs. */
typedef struct SdExpression {
  size_t n_operations;
  SdOperation *operations;
} SdExpression;

#endif  // UNIT_TESTS


#endif  // _SPRAY_DWARF_H_
