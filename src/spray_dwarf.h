/* Spray's wrapper around libdwarf. */

#pragma once

#ifndef _SPRAY_SPRAY_DWARF_H_
#define _SPRAY_SPRAY_DWARF_H_

#include "ptrace.h"
#include "spray_elf.h" /* `ElfFile` in `SdLocEvalCtx` */
#include "registers.h" /* `x86_reg` in `SdLocation` */

#include <dwarf.h>
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

/*
 `DW_AT_location` of DIEs that represent runtime variables.
  It can be used in combination with `sd_init_loclist` to
  initialize a new location list.

  `sd_init_loc_attr` is the only way to initialize this struct,
  because this function ensures that the form bounds are met for
  the `loc_attr` field.
*/
typedef struct {
  Dwarf_Attribute loc;		/* `DW_AT_location` attribute. */
} SdLocattr;

typedef struct {
  enum {
    BASE_TYPE_CHAR,
    BASE_TYPE_SIGNED_CHAR,
    BASE_TYPE_UNSIGNED_CHAR,
    BASE_TYPE_SHORT,
    BASE_TYPE_UNSIGNED_SHORT,
    BASE_TYPE_INT,
    BASE_TYPE_UNSIGNED_INT,
    BASE_TYPE_LONG,
    BASE_TYPE_UNSIGNED_LONG,
    BASE_TYPE_LONG_LONG,
    BASE_TYPE_UNSIGNED_LONG_LONG,
    BASE_TYPE_FLOAT,
    BASE_TYPE_DOUBLE,
    BASE_TYPE_LONG_DOUBLE,
  } tag;
  unsigned char size;		/* Number of bytes used to represent this base type. */
} SdBasetype;

/* See the DWARF 5 standard 5.3. */
typedef enum {
  TYPE_MOD_ATOMIC = DW_TAG_atomic_type,
  TYPE_MOD_CONST = DW_TAG_const_type,
  TYPE_MOD_POINTER = DW_TAG_pointer_type,
  TYPE_MOD_RESTRICT = DW_TAG_restrict_type,
  TYPE_MOD_VOLATILE = DW_TAG_volatile_type,
} SdTypemod;

/* Single node in the representation variable types. */
typedef struct {
  enum {
    NODE_BASE_TYPE,
    NODE_MODIFIER,
  } tag;			/* Kind of this node. */
  union {
    SdBasetype base_type;
    SdTypemod modifier;
  };
} SdTypenode;

/* Host structure for variable types. */
typedef struct {
  SdTypenode *nodes;		/* Buffer of nodes. */
  size_t n_nodes;		/* First `n` nodes in use. */
  size_t n_alloc;		/* Maximum number of nodes. */
} SdType;

void del_type(SdType *type);

/*
 Representation of runtime variables. They are used to find the
 location of the variable's value in the running program, and
 to find out what type the variable has.

 `SdLocattr`'s memory is handled by `libdwarf`. Only `SdType`
 must be deleted after it's been used by the caller.
*/
typedef struct {
  SdLocattr loc;		/* Runtime location. */
  SdType type;		/* Type. */
} SdVarattr;

SprayResult sd_init_loc_attr(Dwarf_Debug dbg,
			     Dwarf_Die die,
			     Dwarf_Attribute loc,
			     SdLocattr *attr_dest);

/*
  Get the attributes describing the variable with the given
  name, and the file and line where this variable was declared.
  `pc` is used to choose the closest variable if the variable
  name occurs more than once.

  On success `SP_OK` is returned, and `attr`, `decl_file`, and
  `decl_line` are set. `decl_file` must be `free`'d manually by
  this function's caller.

  On error `SP_ERR` is returned, and `attr`, `decl_file`, and
  `decl_file` remain unchanged.

  `dbg`, `var_name`, `attr`, `decl_file`, and `decl_line` must
  not be `NULL`.
*/
SprayResult sd_runtime_variable(Dwarf_Debug dbg,
				dbg_addr pc,
				const char *var_name,
				SdVarattr *attr,
				char **decl_file,
				unsigned *decl_line);

/*
 Initialize a location list from the location
 description attribute in `loc_attr`.
*/
SprayResult sd_init_loclist(Dwarf_Debug dbg,
			    SdLocattr loc_attr,
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
