#include "spray_dwarf.h"

#include "magic.h"

#include <dwarf.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef bool (*SearchCallback)(Dwarf_Debug, Dwarf_Die, const void *const, void *const);

Dwarf_Debug dwarf_init(const char *restrict filepath, Dwarf_Error *error) {  
  assert(filepath != NULL);
  assert(error != NULL);

  /* Standard group number. Group numbers are relevant only if
     DWARF debug information is split accross multiple objects. */
  unsigned groupnumber = DW_GROUPNUMBER_ANY;

  /* `true_path*` is used to store the path of alternate
     DWARF-containing objects if debug info is split.
     Setting both to 0 disables looking for separate info.
     Use an array of size `MAX_PATHLEN` if `true_pathbuf`
     is in use. */
  static char *true_pathbuf = NULL;
  unsigned true_pathlen = 0;

  /* Using the error handler is not recommended.
     If it were used, `error_argument` could pass
     the error handler callback some extra data. */
  Dwarf_Handler error_handler = NULL;
  Dwarf_Ptr error_argument = NULL;

  /* Holds the debugger on success. */
  Dwarf_Debug dbg = NULL;

  int res = dwarf_init_path(filepath,  /* Only external paramter. */
    true_pathbuf, true_pathlen,
    groupnumber,
    error_handler, error_argument,  /* Both `NULL` => unused. */
    &dbg, error
  );

  if (res == DW_DLV_ERROR || res == DW_DLV_NO_ENTRY) {
    return NULL;
  } else {
    /* `res == DW_DLV_OK`, success! */
    return dbg;
  }
}

/* NOTE: The return value of `search_dwarf_die` and `search_dwarf_dbg`
   only signals the outcome of the libdwarf calls. Therefore the return
   value `DW_DLV_OK` doesn't mean that the search was successful in that
   we found something. It only means that there wasn't a error with DWARF. */

int sd_search_dwarf_die(Dwarf_Debug dbg, Dwarf_Die in_die, Dwarf_Error *const error,
  int is_info, int in_level,
  SearchCallback search_callback,
  const void *const search_for, void *const search_findings
) {  
  int res = DW_DLV_OK;
  Dwarf_Die cur_die = in_die;
  Dwarf_Die child_die = NULL;

  bool in_die_found = search_callback(dbg, in_die, search_for, search_findings);
  if (in_die_found) {
    return DW_DLV_OK;
  }

  while (1) {
    Dwarf_Die sib_die = NULL;

    res = dwarf_child(cur_die, &child_die, error);
    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_OK) {
      /* We found a child: recurse! */
      res = sd_search_dwarf_die(dbg, child_die, error,
        is_info, in_level + 1,
        search_callback, search_for, search_findings);
  
      dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
      child_die = NULL;

      if (res == DW_DLV_ERROR) {
        return DW_DLV_ERROR;
      }
    }

    /* `DW_DLV_OK` or `DW_DLV_NO_ENTRY`. */

    res = dwarf_siblingof_b(dbg, cur_die,
      is_info, &sib_die,
      error);

    /* Is `cur_die` a sibling of the initial DIE? */
    if (cur_die != in_die) {
      dwarf_dealloc(dbg, cur_die, DW_DLA_DIE);
      cur_die = NULL;
    }

    if (res == DW_DLV_NO_ENTRY) {
      /* Level is empty now. */
      return DW_DLV_OK;
    } else if (res == DW_DLV_ERROR){
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_OK) {
      cur_die = sib_die;
      bool sib_die_found = search_callback(dbg, sib_die, search_for, search_findings);
      if (sib_die_found) {
        return DW_DLV_OK;
      }
    }
  }
}

/* Search the `Dwarf_Debug` instance for `search_for`. For each DIE
   `search_callback` is called and passed `search_for` and `search_findings`
   in that order. It checks if `search_for` is found in the DIE and stores
   the findins in `search_findings`.
   `search_callback` returns `true` if `search_for` has been found. */
int sd_search_dwarf_dbg(Dwarf_Debug dbg, Dwarf_Error *const error,
  SearchCallback search_callback,
  const void *const search_for, void *const search_findings
) {
  Dwarf_Half version_stamp = 0;  /* Store version number (2 - 5). */
  Dwarf_Unsigned abbrev_offset = 0; /* .debug_abbrev offset from CU just read. */
  Dwarf_Half address_size = 0;  /* CU address size (4 or 8). */
  Dwarf_Half offset_size = 0;  /* Length of the size field in the CU header. */
  Dwarf_Half extension_size = 0;  /* 4 for standard 64-bit DWARF. Zero otherwise. */
  Dwarf_Sig8 signature;
  Dwarf_Unsigned typeoffset = 0;
  Dwarf_Unsigned next_cu_header = 0;  /* Offset in section of next CU. Not of interest. */
  Dwarf_Half header_cu_type = 0;  /* Some DW_UT value. */
  Dwarf_Bool is_info = true;  /* False only if reading through DWARF4 .debug_types. */
  int res = 0;

  while (1) {
    Dwarf_Die cu_die = NULL;  /* Compilation unit DIE. */
    Dwarf_Unsigned cu_header_len = 0;

    memset(&signature, 0, sizeof(signature));

    res = dwarf_next_cu_header_d(dbg,
      is_info, &cu_header_len,
      &version_stamp, &abbrev_offset,
      &address_size, &offset_size,
      &extension_size, &signature,
      &typeoffset, &next_cu_header,
      &header_cu_type, error);

    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_NO_ENTRY) {
      if (is_info == true) {
        /* Nothing left in .debug_info.
           Continue with .debug_types. */
        is_info = false;
        continue;
      }

      /* Done, everything has been checked. */
      return DW_DLV_OK;
    }

    /* Get the CU DIE. */
    res = dwarf_siblingof_b(dbg, NULL,  /* <- Pass NULL to get the CU DIE. */
      is_info, &cu_die,
      error);

    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_NO_ENTRY) {
      /* Impossible: every CU has a single DIE. */
      return DW_DLV_NO_ENTRY;
    }

    res = sd_search_dwarf_die(dbg, cu_die, error, is_info, 0,
      search_callback, search_for, search_findings);

    dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
    cu_die = NULL;

    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    }
  }
}

int sd_get_high_and_low_pc(Dwarf_Die die, Dwarf_Error *const error,
  Dwarf_Addr *lowpc, Dwarf_Addr *highpc
) {
  int res = 0;

  Dwarf_Addr lowpc_buf;
  res = dwarf_lowpc(die, &lowpc_buf, error);

  if (res != DW_DLV_OK) {
    return res;
  }

  Dwarf_Half high_form = 0;
  enum Dwarf_Form_Class high_class = DW_FORM_CLASS_UNKNOWN;

  Dwarf_Addr highpc_buf;
  res = dwarf_highpc_b(die,
    &highpc_buf, &high_form,
    &high_class, error);

  if (res != DW_DLV_OK) {
    return res;
  }

  if (
    high_form != DW_FORM_addr &&
    !dwarf_addr_form_is_indexed(high_form)
  ) {
    /* `highpc_buf` is an offset of `lowpc_buf`. */
    highpc_buf += lowpc_buf;
  }

  *lowpc = lowpc_buf;
  *highpc = highpc_buf;

  return res;
}

/* Switches `pc_in_die` to `true` if it finds that `pc` lies
   between `DW_AT_pc_low` and `DW_AT_pc_high` of the DIE. */
int sd_check_pc_in_die(Dwarf_Die die, Dwarf_Error *const error, Dwarf_Addr pc, bool *pc_in_die) {
  int res = DW_DLV_OK;
  Dwarf_Half die_tag = 0;
  res = dwarf_tag(die,
    &die_tag, error);

  if (res != DW_DLV_OK) {
    return res;
  }

  if (die_tag == DW_TAG_subprogram) {
    Dwarf_Addr lowpc, highpc;
    res = sd_get_high_and_low_pc(die, error, &lowpc, &highpc);
    if (res != DW_DLV_OK) {
      return res;
    } else if (lowpc <= pc && pc <= highpc) {
      *pc_in_die = true;
    }
  }

  return DW_DLV_OK;
}

/* Search callback which receives a `Dwarf_Addr*`
   representing a PC value as search data and finds
   a `char **` whose internal value representens the
   function name corresponding to the PC.
   Allocated memory for the function name of it returns
   `true`. This memory must be released by the caller. */
bool callback__find_subprog_name_by_pc(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for;
  char **fn_name = (char **) search_findings;

  int res = DW_DLV_OK;
  Dwarf_Error error = NULL;
  bool found_pc = false;

  res = sd_check_pc_in_die(die, &error, *pc, &found_pc);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  }

  if (!found_pc) {
    return false;
  } else {
    /* Found PC in this DIE. */
    Dwarf_Half attrnum = DW_AT_name;
    char *attr_buf;

    res = dwarf_die_text(die, attrnum,
      &attr_buf, &error);

    if (res == DW_DLV_OK) {
      size_t len = strlen(attr_buf);
      *fn_name = (char *) realloc (*fn_name, len + 1);
      strcpy(*fn_name, attr_buf);
      return true;
    } else {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return false;
    }
  }
}

char *get_function_from_pc(Dwarf_Debug dbg, x86_addr pc) {
  Dwarf_Error error = NULL;

  Dwarf_Addr pc_addr = pc.value;
  char *fn_name = NULL;  // <- Store the function name here.
  
  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__find_subprog_name_by_pc,
    &pc_addr,
    &fn_name);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
    return NULL;
  } else if (res == DW_DLV_NO_ENTRY) {
    return NULL;
  } else {
    /* Search completed without error. `fn_name`
       might still be NULL if it wasn't found. */
    return fn_name;
  }
}

/* NOTE: Acquiring a `Dwarf_Line_Context` is only possilbe
   if the given DIE is the compilation unit DIE. */
bool sd_get_line_context(Dwarf_Debug dbg, Dwarf_Die cu_die, Dwarf_Line_Context *line_context) {
  assert(dbg != NULL);
  assert(cu_die != NULL);
  assert(line_context != NULL);  
  int res;
  Dwarf_Unsigned line_table_version = 0;
  Dwarf_Small line_table_count = 0;  /* 0 and 1 are normal. 2 means
                                      experimental two-level line table. */
  Dwarf_Line_Context line_context_buf = NULL;
  Dwarf_Error error = NULL;

  res = dwarf_srclines_b(cu_die,
    &line_table_version, &line_table_count,
    &line_context_buf, &error);

  if (res != DW_DLV_OK) {
    if (DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  } else if (line_table_count == 2) {
    /* two-level line table is not currently supported. */
    return false;
  }

  *line_context = line_context_buf;
  
  return true;
}

bool sd_get_srclines(
  Dwarf_Debug dbg,
  Dwarf_Die cu_die,
  Dwarf_Line **lines,
  Dwarf_Signed *n_lines,
  Dwarf_Line_Context *line_context
) {
  assert(lines != NULL);
  assert(n_lines != NULL);
  assert(line_context != NULL);

  int res = 0;
  Dwarf_Error error = NULL;

  Dwarf_Line_Context line_context_buf = NULL;
  if (!sd_get_line_context(dbg, cu_die, &line_context_buf)) {
    return false;
  }

  Dwarf_Line *lines_buf = NULL;
  Dwarf_Signed n_lines_buf = 0;

  res = dwarf_srclines_from_linecontext(line_context_buf,
    &lines_buf, &n_lines_buf,
    &error);

  if (res != DW_DLV_OK) {
    dwarf_srclines_dealloc_b(line_context_buf);
    if (DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  } else {
    *lines = lines_buf;
    *n_lines = n_lines_buf;
    *line_context = line_context_buf;
    return true;
  }
}

bool callback__find_line_entry_by_pc(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for;
  LineEntry *line_entry = (LineEntry *) search_findings;

  int res = 0;
  Dwarf_Error error = NULL;
  Dwarf_Line *lines = NULL;
  Dwarf_Signed n_lines = 0;
  Dwarf_Line_Context line_context = NULL;

  if (sd_get_srclines(dbg, die, &lines, &n_lines, &line_context)) {
    for (unsigned i = 0; i < n_lines; i++) {
      Dwarf_Addr line_addr = 0;
      res = dwarf_lineaddr(lines[i], &line_addr,
        &error);

      if (res != DW_DLV_OK) {
        break;
      }

      if (line_addr == *pc) {
        Dwarf_Unsigned lineno;
        res = dwarf_lineno(lines[i], &lineno,
          &error);

        if (res != DW_DLV_OK) {
          break;
        }

        /* `dwarf_lineoff_b` returns the column number. */
        Dwarf_Unsigned colno;
        res = dwarf_lineoff_b(lines[i], &colno,
          &error);

        if (res != DW_DLV_OK) {
          break;
        }

        Dwarf_Addr addr;
        res = dwarf_lineaddr(lines[i], &addr, &error);

        if (res != DW_DLV_OK) {
          break;
        }

        /* `dwarf_linesrc` returns the file name.
           `line_entryr->filepath` is set here on succcess. */
        res = dwarf_linesrc(lines[i], &line_entry->filepath,
          &error);

        if (res != DW_DLV_OK) {
          break;
        }

        dwarf_srclines_dealloc_b(line_context);

        line_entry->is_ok = true;
        line_entry->ln = lineno;
        line_entry->cl = colno;
        line_entry->addr = (x86_addr) { addr };
        return true;
      }
    }
  }

  dwarf_srclines_dealloc_b(line_context);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
  }

  /* Reached end of loop without finding
     a line entry with the address. */
  line_entry->is_ok = false;
  return false;
}

LineEntry get_line_entry_from_pc(Dwarf_Debug dbg, x86_addr pc) {
  Dwarf_Error error = NULL;

  Dwarf_Addr pc_addr = pc.value;
  LineEntry line_entry = { .is_ok=false };

  /* Because all fields in `LineEntry` are const
     `find_line_entry_in_die` will cast them to
     be mutable and modify them. */
  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__find_line_entry_by_pc,
    &pc_addr,
    &line_entry);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
    return (LineEntry) { .is_ok=false };
  } else if (res == DW_DLV_NO_ENTRY) {
    return (LineEntry) { .is_ok=false };
  } else {
    /* No DWARF error during search. The returned
       line entry might still have `is_set=false`. */
    return line_entry;
  }
}

bool sd_is_subprog_with_name(Dwarf_Debug dbg, Dwarf_Die die, const char *name) {
  assert(dbg != NULL);
  assert(die != NULL);
  assert(name != NULL);

  int res = 0;
  Dwarf_Error error;

  Dwarf_Half die_tag = 0;
  res = dwarf_tag(die,
    &die_tag, &error);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  }

  /* Is the given DIE about a function? */
  if (die_tag == DW_TAG_subprogram) {
    /* Check if the function name matches */
    char *fn_name_buf = NULL;
    res = dwarf_die_text(die, DW_AT_name,  
      &fn_name_buf, &error);

    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return false;
    }

    /* Do the names match? */
    if (strcmp(fn_name_buf, name) == 0) {
      return true;
    } else {
      return false;
    }
  } else {
    /* This DIE is not a subprogram. */
    return false;
  }
}

typedef struct {
  /* Addresses are unsigned and we should allow them
     to have any value. Therefore `is_set` signals
     whether or not they are set. The alternative of
     using e.g. `-1` as the unset value doesn't work. */
  bool is_set;
  x86_addr lowpc;
  x86_addr highpc;
} SubprogAttr;

/* Search callback that looks for a DIE describing the
   subprogram with the name `search_for` and stores the
   attributes `AT_low_pc` and `AT_high_pc` in `search_findings`. */
bool find_subprog_attributes_in_die(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  const char *fn_name = (const char *) search_for;
  SubprogAttr *attr = (SubprogAttr *) search_findings;

  if (sd_is_subprog_with_name(dbg, die, fn_name)) {
    int res = 0;
    Dwarf_Error error;
    Dwarf_Addr lowpc, highpc;
    res = sd_get_high_and_low_pc(die, &error, &lowpc, &highpc);

    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return false;
    }

    attr->lowpc = (x86_addr) { lowpc };
    attr->highpc = (x86_addr) { highpc };
    attr->is_set = true;
    return true;
  } else {
    return false;
  }
}

SubprogAttr sd_get_subprog_attr(Dwarf_Debug dbg, const char* fn_name) {
  assert(dbg != NULL);
  assert(fn_name != NULL);

  Dwarf_Error error = NULL;
  SubprogAttr attr = { .is_set=false };

  int res = sd_search_dwarf_dbg(dbg, &error,  
    find_subprog_attributes_in_die,
    fn_name, &attr);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return (SubprogAttr) { .is_set=false };
  } else {
    return attr;
  }
}

/* Get the `low_pc` and `high_pc` attributes for the
   subprogram with the given name. Returns `true` if
   attribute was found.
   Both `get_at_*_pc` functions are unused and not exposed
   globally. They may be useful again though. */
bool get_at_low_pc(Dwarf_Debug dbg, const char* fn_name, x86_addr *lowpc_dest) {
  assert(dbg != NULL);
  assert(lowpc_dest != NULL);

  if (fn_name == NULL) {
    return false;
  }

  SubprogAttr attr = sd_get_subprog_attr(dbg, fn_name);
  if (attr.is_set) {
    lowpc_dest->value = attr.lowpc.value;
    return true;
  } else {
    return false;
  }
}

bool get_at_high_pc(Dwarf_Debug dbg, const char *fn_name, x86_addr *highpc_dest) {  
  assert(dbg != NULL);
  assert(fn_name != NULL);
  assert(highpc_dest != NULL);

  SubprogAttr attr = sd_get_subprog_attr(dbg, fn_name);
  if (attr.is_set) {
    highpc_dest->value = attr.highpc.value;
    return true;
  } else {
    return false;
  }
}

typedef struct {
  line_callback callback;
  void *const data;
} CallbackAndData;

bool callback__run_callback_on_subprog(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  const char *fn_name = (const char *) search_for;
  CallbackAndData *cad = (CallbackAndData *) search_findings;

  if (sd_is_subprog_with_name(dbg, die, fn_name)) {
    Dwarf_Error error = NULL;
    int res = 0;
    Dwarf_Line *lines = NULL;
    Dwarf_Signed n_lines = 0;
    Dwarf_Line_Context line_context = NULL;

    if (sd_get_srclines(dbg, die, &lines, &n_lines, &line_context)) {
      for (unsigned i = 0; i < n_lines; i++) {
        res = cad->callback(lines[i], cad->data, &error);
        if (res == DW_DLV_ERROR) {
          dwarf_dealloc_error(dbg, error);
          error = NULL;
        }
        res = DW_DLV_OK;
      }
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

void for_each_line_in_subprog(
  Dwarf_Debug dbg,
  const char *fn_name,
  line_callback callback,
  void *const init_data
) {
  assert(dbg != NULL);
  assert(callback != NULL);

  Dwarf_Error error = NULL;
  CallbackAndData cad = {
    callback,
    init_data,
  };

  int res = sd_search_dwarf_dbg(
    dbg, &error,
    callback__run_callback_on_subprog,
    fn_name,
    &cad
  );

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
  }
}
