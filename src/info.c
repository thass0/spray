#include "info.h"

#include "spray_dwarf.h"
#include "spray_elf.h"

#include <assert.h>
#include <string.h>

struct DebugSymbol
{
  /* This `Debug_Symbol`'s index into the `DebugSymbolBuf`
   * that it was allocated by. This member can be used to
   * get mutable access to a `const DebugSymbol *`. */
  const size_t buf_idx;

  const Elf64_Sym *elf;

  /* `Elf64_Sym` symbols only store an address range
   * that belongs to the symbol. If the symbol was
   * created with a specific address it is store in `addr`. */
  struct
  {
    /* NOTE: `has_addr` must always be checked before
     * reading `addr`. If `has_addr` is false `addr`
     * doesn't have a meaningful value. */
    bool has_addr;
    dbg_addr addr;
  };

  /* Information about the part of the source
   * code that is the origin of the symbol. */
  struct
  {
    /* NOTE: `has_position` must be true for `position`
     * to have a meaningful value. Check it before
     * reading `position`. */
    bool has_position;
    Position position;
  };
  char *filepath;
};

enum
{
  SYM_ALLOC_SIZE = 16,
};

typedef struct
{
  size_t n_symbols;
  size_t n_alloc;
  DebugSymbol *syms;
} DebugSymbolBuf;

DebugSymbolBuf *
init_symbol_buf (void)
{
  DebugSymbolBuf *buf = malloc (sizeof (DebugSymbolBuf));
  if (buf == NULL)
    {
      return NULL;
    }
  buf->n_symbols = 0;
  buf->n_alloc = SYM_ALLOC_SIZE;

  buf->syms = calloc (buf->n_alloc, sizeof (*buf->syms));
  if (buf->syms == NULL)
    {
      free (buf);
      return NULL;
    }

  return buf;
}

/* Allocate a new symbol. Set all its members to 0. */
DebugSymbol *
alloc_symbol (DebugSymbolBuf *buf)
{
  assert (buf != NULL);
  if (buf->n_symbols >= buf->n_alloc)
    {
      buf->n_alloc += SYM_ALLOC_SIZE;
      buf->syms = realloc (buf->syms, sizeof (*buf->syms) * buf->n_alloc);
      assert (buf->syms != NULL);
    }

  DebugSymbol *sym = &buf->syms[buf->n_symbols];
  memset (sym, 0, sizeof (*sym));

  /* Initialize the const member `buf_idx` (it's not
   * UB to cast const members of non-const memory like
   * the memory returned by malloc). */
  *(size_t *) &sym->buf_idx = buf->n_symbols;

  buf->n_symbols++;
  return sym;
}

void
free_symbol_buf (DebugSymbolBuf **bufp)
{
  assert (bufp != NULL);

  if (*bufp != NULL)
    {
      DebugSymbolBuf *buf = *bufp;
      for (size_t i = 0; i < buf->n_symbols; i++)
	{
	  free (buf->syms[i].filepath);
	}
      free (buf->syms);
      buf->syms = NULL;
      buf->n_symbols = 0;
      buf->n_alloc = 0;
      free (buf);
      *bufp = NULL;
    }
}

/* Return a non-const pointer to the same symbol. The pointer
 * returned by this function points to the exact same memory
 * as the const version does. */
DebugSymbol *
mut_sym_ptr (const DebugSymbol *sym, DebugSymbolBuf *buf)
{
  if (buf == NULL || sym == NULL)
    {
      return NULL;
    }
  else
    {
      return &buf->syms[sym->buf_idx];
    }
}

struct DebugInfo
{
  ElfFile *elf;
  Dwarf_Debug dbg;
  DebugSymbolBuf *symbols;
};

DebugInfo *
init_debug_info (const char *filepath)
{
  if (filepath == NULL)
    {
      return NULL;
    }

  Dwarf_Error error = NULL;
  Dwarf_Debug dbg = sd_dwarf_init (filepath, &error);
  if (dbg == NULL)
    {
      dwarf_dealloc_error (NULL, error);
      return NULL;
    }

  ElfFile *elf = malloc (sizeof (*elf));
  if (elf == NULL)
    {
      return NULL;
    }
  ElfParseResult elf_res = se_parse_elf (filepath, elf);
  if (elf_res == SP_ERR)
    {
      free (elf);
      dwarf_finish (dbg);
      return NULL;
    }

  DebugSymbolBuf *buf = init_symbol_buf ();
  if (buf == NULL)
    {
      free (elf);
      dwarf_finish (dbg);
      return NULL;
    }

  DebugInfo *info = malloc (sizeof (*info));
  if (info == NULL)
    {
      free_symbol_buf (&buf);
      free (elf);
      dwarf_finish (dbg);
      return NULL;
    }

  info->elf = elf;
  info->dbg = dbg;
  info->symbols = buf;

  return info;
}

SprayResult
free_debug_info (DebugInfo **infop)
{
  assert (infop != NULL);

  if (*infop != NULL)
    {
      DebugInfo *info = *infop;
      ElfFile elf = *info->elf;
      dwarf_finish (info->dbg);
      free_symbol_buf (&info->symbols);
      free (info->elf);
      free (info);
      *infop = NULL;
      return se_free_elf (elf);
    }
  else
    {
      return SP_OK;
    }
}

const DebugSymbol *
sym_by_name (const char *name, DebugInfo *info)
{
  if (info == NULL)
    {
      return NULL;
    }

  const Elf64_Sym *elf = se_symbol_from_name (name, info->elf);
  if (elf == NULL)
    {
      return NULL;
    }

  DebugSymbol *sym = alloc_symbol (info->symbols);
  sym->elf = elf;
  return sym;
}

const DebugSymbol *
sym_by_addr (dbg_addr addr, DebugInfo *info)
{
  if (info == NULL)
    {
      return NULL;
    }

  const Elf64_Sym *elf = se_symbol_from_addr (addr, info->elf);
  if (elf == NULL)
    {
      return NULL;
    }

  DebugSymbol *sym = alloc_symbol (info->symbols);
  sym->elf = elf;
  sym->has_addr = true;
  sym->addr = addr;
  return sym;
}

const char *
sym_name (const DebugSymbol *sym, const DebugInfo *info)
{
  if (sym == NULL || info == NULL)
    {
      return NULL;
    }

  return se_symbol_name (sym->elf, info->elf);
}

SprayResult
function_start_addr (const DebugSymbol *func,
		     const DebugInfo *info, dbg_addr *addr)
{
  if (func == NULL || info == NULL || addr == NULL)
    {
      return SP_ERR;
    }
  else
    {
      if (se_symbol_type (func->elf) == STT_FUNC)
	{
	  return sd_effective_start_addr (info->dbg,
					  sym_start_addr (func),
					  sym_end_addr (func), addr);
	}
      else
	{
	  return SP_ERR;
	}
    }
}

dbg_addr
sym_start_addr (const DebugSymbol *sym)
{
  if (sym == NULL)
    {
      return (dbg_addr)
      {
      0};
    }
  else
    {
      return se_symbol_start_addr (sym->elf);
    }
}

dbg_addr
sym_end_addr (const DebugSymbol *sym)
{
  if (sym == NULL)
    {
      return (dbg_addr)
      {
      0};
    }
  else
    {
      return se_symbol_end_addr (sym->elf);
    }
}

dbg_addr
sym_addr (const DebugSymbol *sym)
{
  if (sym == NULL)
    {
      return (dbg_addr)
      {
      0};
    }
  else
    {
      if (sym->has_addr)
	{
	  return sym->addr;
	}
      else
	{
	  /* Return the start of the range of addresses that
	   * belongs to the symbol if the symbol doesn't have
	   * a specific address. */
	  return sym_start_addr (sym);
	}
    }
}

/* Check to see if the symbol has a specific address or
 * was created from a name and thus only has a range of
 * addresses that belong to it.
 * `sym_addr == sym_start_addr` is true for the given symbol
 * if this function returns false. */
bool
uses_specific_address (const DebugSymbol *sym)
{
  if (sym != NULL && sym->has_addr)
    {
      return true;
    }
  else
    {
      return false;
    }
}

const char *
sym_filepath (const DebugSymbol *sym, const DebugInfo *info)
{
  if (sym == NULL || info == NULL)
    {
      return NULL;
    }

  if (sym->filepath != NULL)
    {
      return sym->filepath;
    }
  else
    {
      char *filepath = sd_filepath_from_pc (info->dbg, sym_addr (sym));
      if (filepath == NULL)
	{
	  return NULL;
	}
      else
	{
	  DebugSymbol *mut_sym = mut_sym_ptr (sym, info->symbols);
	  mut_sym->filepath = filepath;
	  return filepath;
	}
    }
}

const Position *
sym_position (const DebugSymbol *sym, const DebugInfo *info)
{
  if (sym == NULL || info == NULL)
    {
      return NULL;
    }

  if (sym->has_position)
    {
      return &sym->position;
    }
  else
    {
      LineEntry line_entry =
	sd_line_entry_from_pc (info->dbg, sym_addr (sym));
      if (!line_entry.is_ok)
	{
	  return NULL;
	}
      else
	{
	  DebugSymbol *mut_sym = mut_sym_ptr (sym, info->symbols);
	  mut_sym->has_position = true;
	  mut_sym->position = (Position)
	  {
	  .line = line_entry.ln,.column = line_entry.cl,.is_exact =
	      line_entry.is_exact,};

	  return &sym->position;
	}
    }
}

const Position *
addr_position (dbg_addr addr, DebugInfo *info)
{
  const DebugSymbol *addr_sym = sym_by_addr (addr, info);
  if (addr_sym == NULL)
    {
      return NULL;
    }
  else
    {
      return sym_position (addr_sym, info);
    }
}

const char *
addr_name (dbg_addr addr, DebugInfo *info)
{
  const DebugSymbol *addr_sym = sym_by_addr (addr, info);
  if (addr_sym == NULL)
    {
      return NULL;
    }
  else
    {
      return sym_name (addr_sym, info);
    }
}

const char *
addr_filepath (dbg_addr addr, DebugInfo *info)
{
  const DebugSymbol *addr_sym = sym_by_addr (addr, info);
  if (addr_sym == NULL)
    {
      return NULL;
    }
  else
    {
      return sym_filepath (addr_sym, info);
    }
}

SprayResult
addr_at (const char *filepath,
	 uint32_t lineno, const DebugInfo *info, dbg_addr *addr)
{
  if (filepath == NULL || info == NULL || addr == NULL)
    {
      return SP_ERR;
    }
  else
    {
      LineEntry line = sd_line_entry_at (info->dbg, filepath, lineno);
      if (line.is_ok)
	{
	  *addr = line.addr;
	  return SP_OK;
	}
      else
	{
	  return SP_ERR;
	}
    }
}

bool
is_dyn_exec (const DebugInfo *info)
{
  if (info == NULL)
    {
      return false;
    }
  else
    {
      return info->elf->type == ELF_TYPE_DYN;
    }
}

/* `break_scope_around_sym` places a breakpoint on each line
 *  in the function belonging to the symbol `func`. The only
 * line that doesn't get a breakpoint is the line that `func`
 * itself refers to. This is used to implement stepping over
 * a line. */

typedef struct
{
  size_t to_del_alloc;
  size_t to_del_idx;
  real_addr *to_del;
  real_addr load_address;
  unsigned skip_line;
  Breakpoints *breakpoints;
} CallbackData;

enum
{ TO_DEL_ALLOC_SIZE = 16 };

SprayResult
callback__set_dwarf_line_breakpoint (LineEntry *line, void *const void_data)
{
  assert (line != NULL);
  assert (void_data != NULL);

  CallbackData *data = (CallbackData *) void_data;

  real_addr real_line_addr = { line->addr.value - data->load_address.value };

  if (data->skip_line != line->ln &&
      !lookup_breakpoint (data->breakpoints, real_line_addr))
    {
      enable_breakpoint (data->breakpoints, real_line_addr);

      if (data->to_del_idx >= data->to_del_alloc)
	{
	  data->to_del_alloc += TO_DEL_ALLOC_SIZE;
	  data->to_del =
	    realloc (data->to_del, sizeof (real_addr) * data->to_del_alloc);
	  assert (data->to_del != NULL);
	}
      data->to_del[data->to_del_idx++] = real_line_addr;
    }

  return SP_OK;
}

/* Set breakpoints for `step_over`. If this function succeeds, it will
 * allocate an array in `*to_del_ptr` which holds the addresses of
 * `*n_to_del` breakpoints. The caller has to free `*to_del_ptr` and
 * delete all breakpoints. */
SprayResult
set_step_over_breakpoints (const DebugSymbol *func,
			   const DebugInfo *info,
			   real_addr load_address,
			   Breakpoints *breakpoints,
			   real_addr **to_del_ptr, size_t *n_to_del)
{
  if (func == NULL || info == NULL || breakpoints == NULL ||
      to_del_ptr == NULL || n_to_del == NULL)
    {
      return SP_ERR;
    }

  const Position *pos = sym_position (func, info);
  const char *func_name = sym_name (func, info);
  const char *filepath = sym_filepath (func, info);
  if (pos == NULL || func_name == NULL || filepath == NULL)
    {
      return SP_ERR;
    }

  CallbackData data = {
    .to_del_alloc = TO_DEL_ALLOC_SIZE,
    .to_del_idx = 0,
    .to_del = calloc (TO_DEL_ALLOC_SIZE, sizeof (real_addr)),
    .load_address = load_address,
    .skip_line = pos->line,
    .breakpoints = breakpoints,
  };

  if (data.to_del == NULL)
    {
      return SP_ERR;
    }

  sd_for_each_line (info->dbg, func_name, filepath,
		    callback__set_dwarf_line_breakpoint, &data);

  *n_to_del = data.to_del_idx;
  *to_del_ptr = data.to_del;

  return SP_OK;
}

typedef struct RuntimeVariable
{
  SdLocation loc;
  char *decl_file;		/* The file where the variable was declared. */
  unsigned decl_line;		/* The line where the variable was declared. */
  SdType type;
} RuntimeVariable;

real_addr
var_loc_addr (RuntimeVariable *var)
{
  if ((var != NULL) && (var->loc.tag == LOC_ADDR))
    {
      return var->loc.addr;
    }
  else
    {
      return (real_addr)
      {
      0};
    }
}

x86_reg
var_loc_reg (RuntimeVariable *var)
{
  if ((var != NULL) && (var->loc.tag == LOC_REG))
    {
      return var->loc.reg;
    }
  else
    {
      return 0;
    }
}

bool
is_addr_loc (RuntimeVariable *var)
{
  return (var != NULL) && (var->loc.tag == LOC_ADDR);
}

bool
is_reg_loc (RuntimeVariable *var)
{
  return (var != NULL) && (var->loc.tag == LOC_REG);
}

const char *
var_loc_path (RuntimeVariable *var)
{
  assert (var != NULL);
  return var->decl_file;
}

unsigned
var_loc_line (RuntimeVariable *var)
{
  assert (var != NULL);
  return var->decl_line;
}

void
print_var_loc (RuntimeVariable *var)
{
  if (var == NULL)
    {
      printf ("<?>:<?>");
    }
  else
    {
      const char *var_path = var_loc_path (var);
      if (var_path != NULL)
	{
	  print_as_relative_filepath (var_path);
	}
      else
	{
	  printf ("<?>");
	}

      printf (":");

      unsigned var_line = var_loc_line (var);
      if (var_line > 0)
	{			/* Line numbers start at 1! */
	  printf ("%u", var_line);
	}
      else
	{
	  printf ("<?>");
	}
    }
}

void
print_base_type (SdBasetype base_type, uint64_t value, PrintFilter filter)
{
  /* `-1` starts out being all ones. By shifting it to the
   * left by `base_type.size * 8` bits, the first
   * `base_type.size` bits become `0`. Lastly all bits are
   * flipped so that we get a mask for the `base_type.size`
   * lowest bytes. If `base_type.size` is greater than 8,
   * the mask will select everything. */
  unsigned shift_by = base_type.size * 8;
  uint64_t mask = -1;		/* All ones. */
  if (shift_by < (sizeof (mask) * 8))
    {
      /* A left-shift by `>= sizeof(type) * 8` bits is UB. */
      mask = ~(mask << shift_by);
    }
  value &= mask;

  if (filter == PF_NONE)
    {
      switch (base_type.tag)
	{
	case BASE_TYPE_CHAR:
	  printf ("'%c'", (char) value);
	  break;
	case BASE_TYPE_SIGNED_CHAR:
	  printf ("%hhd", (signed char) value);
	  break;
	case BASE_TYPE_UNSIGNED_CHAR:
	  printf ("%hhu", (signed char) value);
	  break;
	case BASE_TYPE_SHORT:
	  printf ("%hd", (short) value);
	  break;
	case BASE_TYPE_UNSIGNED_SHORT:
	  printf ("%hu", (unsigned short) value);
	  break;
	case BASE_TYPE_INT:
	  printf ("%d", (int) value);
	  break;
	case BASE_TYPE_UNSIGNED_INT:
	  printf ("%u", (int) value);
	  break;
	case BASE_TYPE_LONG:
	  printf ("%ld", (long) value);
	  break;
	case BASE_TYPE_UNSIGNED_LONG:
	  printf ("%lu", (unsigned long) value);
	  break;
	case BASE_TYPE_LONG_LONG:
	  printf ("%lld", (long long) value);
	  break;
	case BASE_TYPE_UNSIGNED_LONG_LONG:
	  printf ("%llu", (unsigned long long) value);
	  break;
	case BASE_TYPE_FLOAT:
	  printf ("%f", (float) value);
	  break;
	case BASE_TYPE_DOUBLE:
	  printf ("%f", (double) value);
	  break;
	case BASE_TYPE_LONG_DOUBLE:
	  printf ("%Lf", (long double) value);
	  break;
	}
    }
  else
    {
      print_filtered (value, filter);
    }
}

void
print_var_value (RuntimeVariable *var, uint64_t value, PrintFilter filter)
{
  if (var == NULL)
    {
      print_filtered (value, filter);
      printf (" (no type!)");
    }
  else
    {
      SdTypenode *node = NULL;
      for (size_t i = 0; i < var->type.n_nodes; i++)
	{
	  node = &var->type.nodes[i];
	  switch (node->tag)
	    {
	    case NODE_BASE_TYPE:
	      print_base_type (node->base_type, value, filter);
	      return;
	    case NODE_MODIFIER:
	      if (node->modifier == TYPE_MOD_POINTER)
		{
		  /* Only stop at pointers. */
		  printf ("%p", (void *) value);
		  return;
		}
	      /* Continue iterating, until a printable node has been found. */
	      break;
	    case NODE_UNSPECIFIED:
	      break;
	    case NODE_TYPEDEF:
	      break;
	    }
	}

      /* The loop returns from this function after printing the value.
       * Thus, if we get here, the value could not be printed with any node. */
      print_filtered (value, filter);
      printf (" (no applicable type!)");
    }
}

RuntimeVariable *
init_var (dbg_addr pc,
	  real_addr load_address,
	  const char *var_name, pid_t pid, const DebugInfo *info)
{
  if (var_name == NULL || info == NULL)
    {
      return NULL;
    }

  SdVarattr var_attr = { 0 };
  char *decl_file = NULL;
  unsigned decl_line = 0;
  SprayResult res = sd_runtime_variable (info->dbg,
					 pc,
					 var_name,
					 &var_attr,
					 &decl_file,
					 &decl_line);
  if (res == SP_ERR)
    {
      return NULL;
    }

  /* Evaluate the location description and store it in `loc`. */
  SdLoclist loclist = { 0 };
  res = sd_init_loclist (info->dbg, var_attr.loc, &loclist);
  if (res == SP_ERR)
    {
      return NULL;
    }

  SdLocEvalCtx ctx = {
    .pid = pid,
    .pc = pc,
    .elf = info->elf,
    .load_address = load_address,
  };

  SdLocation loc = { 0 };
  res = sd_eval_loclist (info->dbg, ctx, loclist, &loc);
  del_loclist (&loclist);
  if (res == SP_ERR)
    {
      return NULL;
    }

  RuntimeVariable *var = malloc (sizeof (*var));
  assert (var != NULL);

  var->type = var_attr.type;
  var->loc = loc;
  /* `decl_line` and `decl_file` are both optional, and may
   * be `0` or `NULL` respectively. */
  var->decl_line = decl_line;
  var->decl_file = decl_file;

  return var;
}

void
del_var (RuntimeVariable *loc)
{
  if (loc != NULL)
    {
      del_type (&loc->type);
      free (loc->decl_file);
      free (loc);
    }
}
