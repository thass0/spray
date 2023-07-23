#include "spray_dwarf.h"

#include "magic.h"

#include <dwarf.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#ifndef UNIT_TESTS
/* This type is defined in `spray_dwarf.h` if `UNIT_TESTS` is defined. */
typedef bool (*SearchCallback)(Dwarf_Debug, Dwarf_Die, const void *const, void *const);
#endif  // UNIT_TESTS

/* Only used for debugging (and tests). */
const char *what_dwarf_result(int dwarf_res) {
  switch (dwarf_res) {
    case DW_DLV_OK:
      return "DW_DLV_OK";
    case DW_DLV_ERROR:
      return "DW_DLV_ERROR";
    case DW_DLV_NO_ENTRY:
      return "DW_DLV_NO_ENTRY";
    default:
      return "<not a libdwarf result>";
  }
}

Dwarf_Debug dwarf_init(const char *restrict filepath, Dwarf_Error *error) {  
  assert(filepath != NULL);
  assert(error != NULL);

  /* Standard group number. Group numbers are relevant only if
     DWARF debug information is split across multiple objects. */
  unsigned group_number = DW_GROUPNUMBER_ANY;

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
    group_number,
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

  /* Search self. */
  bool in_die_found = search_callback(dbg, in_die, search_for, search_findings);
  if (in_die_found) {
    return DW_DLV_OK;
  }

  while (1) {
    Dwarf_Die sib_die = NULL;

    /* Search children. */
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
      } else if (res == DW_DLV_OK) {
        return DW_DLV_OK;
      }
    }

    /* Search siblings. */
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
      return DW_DLV_NO_ENTRY;
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
int sd_search_dwarf_dbg(
  Dwarf_Debug dbg, Dwarf_Error *const error,
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
  bool search_finished = false;

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
      if (search_finished) {
        return DW_DLV_OK;
      } else {
        return DW_DLV_NO_ENTRY;
      }
    }

    if (search_finished) {
      /* If the search has finished, i.e. we found what
         we were looking for, we must continue to walk
         all of the CU headers to reset `dwarf_next_cu_header_d`
         to start at the first CU header again. This function
         won't otherwise work again with the same `Dwarf_Debug` instance. */
      continue;
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
    } else if (res == DW_DLV_OK) {
      search_finished = true;
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

  Dwarf_Addr lowpc, highpc;
  res = sd_get_high_and_low_pc(die, error, &lowpc, &highpc);
  if (res != DW_DLV_OK) {
    return res;
  } else if (lowpc <= pc && pc <= highpc) {
    *pc_in_die = true;
  }

  return DW_DLV_OK;
}

bool sd_has_tag(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Half tagnum) {
  assert(dbg != NULL);
  assert(dbg != NULL);

  Dwarf_Error error = NULL;
  int res = DW_DLV_OK;

  Dwarf_Half die_tag = 0;
  res = dwarf_tag(die, &die_tag, &error);
  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  }

  return die_tag == tagnum;
}

/* Search callback which receives a `Dwarf_Addr*`
   representing a PC value as search data and finds
   a `char **` whose internal value represents the
   function name corresponding to the PC.
   Allocated memory for the function name of it returns
   `true`. This memory must be released by the caller. */
bool callback__find_subprog_name_by_pc(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for;
  char **fn_name = (char **) search_findings;

  if (sd_has_tag(dbg, die, DW_TAG_subprogram)) {
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
  } else {
    return false;
  }
}

char *get_function_from_pc(Dwarf_Debug dbg, x86_addr pc) {
  assert(dbg != NULL);
  Dwarf_Error error = NULL;

  Dwarf_Addr pc_addr = pc.value;
  char *fn_name = NULL;  // <- Store the function name here.
  
  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__find_subprog_name_by_pc,
    &pc_addr,
    &fn_name);

  if (res != DW_DLV_OK) {
   if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    } 
    return NULL;
  } else {
    return fn_name;
  }
}

/* NOTE: Acquiring a `Dwarf_Line_Context` is only possible
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

typedef struct {
  bool is_set;
  LineEntry *lines;
  unsigned n_lines;
  Dwarf_Line_Context line_context;
} LineTable;

bool dwarf_bool_to_bool(Dwarf_Bool dwarf_bool) {
  /* The `libdwarf` docs say that `Dwarf_Bool` is
     "A TRUE(non-zero)/FALSE(zero) data item."
     This conversion ensures that libdwarf's
     `TRUE` value is converted to stdbool's
     `true` correctly. */
  return dwarf_bool == 0 ? false : true;
}

void sd_free_line_table(LineTable *line_table) {
  assert(line_table != NULL);
  free(line_table->lines);
  dwarf_srclines_dealloc_b(line_table->line_context);
  *line_table = (LineTable) { 0 };
}

int sd_line_entry_from_dwarf_line(Dwarf_Line line, LineEntry* line_entry, Dwarf_Error *error) {
  assert(line != NULL);
  assert(error != NULL);

  int res = DW_DLV_OK;

  Dwarf_Unsigned lineno;
  res = dwarf_lineno(line, &lineno,
    error);

  if (res != DW_DLV_OK) { return res; }

  /* `dwarf_lineoff_b` returns the column number. */
  Dwarf_Unsigned colno;
  res = dwarf_lineoff_b(line, &colno,
    error);

  if (res != DW_DLV_OK) { return res; }

  Dwarf_Addr addr;
  res = dwarf_lineaddr(line, &addr, error);

  if (res != DW_DLV_OK) { return res; }

  /* `dwarf_linesrc` returns the file name. */
  char *filepath = NULL;
  res = dwarf_linesrc(line, &filepath,
    error);

  if (res != DW_DLV_OK) { return res; }

  Dwarf_Bool new_statement = false;
  res = dwarf_linebeginstatement(line, &new_statement, error);

  if (res != DW_DLV_OK) { return res; }

  Dwarf_Bool prologue_end = false;
  Dwarf_Bool epilogue_begin = false;
  Dwarf_Unsigned isa = 0;
  Dwarf_Unsigned discriminator = 0;
  res = dwarf_prologue_end_etc(line,
                               &prologue_end,
                               &epilogue_begin,
                               &isa,
                               &discriminator,
                               error);
  unused(epilogue_begin);
  unused(isa);
  unused(discriminator);

  if (res != DW_DLV_OK) { return res; }

  line_entry->is_ok = true;
  line_entry->new_statement = dwarf_bool_to_bool(new_statement);
  line_entry->prologue_end = dwarf_bool_to_bool(prologue_end);
  line_entry->ln = lineno;
  line_entry->cl = colno;
  line_entry->addr = (x86_addr) { addr };
  line_entry->filepath = filepath;

  return DW_DLV_OK;
}

bool sd_has_at(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Half attrnum) {
  assert(dbg != NULL);
  assert(dbg != NULL);

  Dwarf_Error error = NULL;
  int res = DW_DLV_OK;

  Dwarf_Bool has_at = false;
  res = dwarf_hasattr(die, attrnum, &has_at, &error);
  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  }

  return dwarf_bool_to_bool(has_at);
}

char *sd_get_filepath(Dwarf_Debug dbg, Dwarf_Die die) {
  assert(dbg != NULL);
  assert(die != NULL);

  Dwarf_Error error = NULL;
  int res = DW_DLV_OK;

  if (
    sd_has_at(dbg, die, DW_AT_name) && 
    sd_has_at(dbg, die, DW_AT_comp_dir)
  ) {
    char *file_name = NULL;
    char *dir_name = NULL;
    /* Get file and dir name for CU DIE. */
    res = dwarf_diename(die, &file_name, &error);
    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return NULL;
    }

    res = dwarf_die_text(die, DW_AT_comp_dir, &dir_name, &error);
    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return NULL;
    }

    char *filepath = (char *) calloc (strlen(dir_name) + strlen(file_name) + 2,
                                     sizeof(char));
    strcpy(filepath, dir_name);
    strcat(filepath, "/");
    strcat(filepath, file_name);
    return filepath;
  } else if (sd_has_at(dbg, die, DW_AT_decl_file)) {
    /* Get file and dir name for normal DIE. */
    char *decl_file = NULL;
    res = dwarf_die_text(die, DW_AT_decl_file, &decl_file, &error);
    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
        dwarf_dealloc_error(dbg, error);
      }
      return NULL;
    }
    /* Must copy because `basename` and `dirname` might modify. */
    return strdup(decl_file);
  } else {
    return NULL;
  }
}

bool callback__find_filepath_by_pc(Dwarf_Debug dbg, Dwarf_Die die,
  const void *const search_for, void *const search_findings
) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for;
  char **filepath = (char **) search_findings;

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
    *filepath = sd_get_filepath(dbg, die);
    return true;
  }
}

char *get_filepath_from_pc(Dwarf_Debug dbg, x86_addr pc) {
  assert(dbg != NULL);
  Dwarf_Error error = NULL;  

  Dwarf_Addr pc_addr = pc.value;
  char *filepath = NULL;

  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__find_filepath_by_pc,
    &pc_addr,
    &filepath);

  if (res != DW_DLV_OK) {
   if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    } 
    return NULL;
  } else {
    return filepath;
  }
}

bool sd_is_die_from_file(Dwarf_Debug dbg, Dwarf_Die die, const char *filepath) {
  assert(dbg != NULL);
  assert(die != NULL);
  assert(filepath != NULL);  

  char *die_filepath = sd_get_filepath(dbg, die);
  if (die_filepath == NULL) {
    return false;
  }
  char *file_name = basename(die_filepath);
  char *dir_name = dirname(die_filepath);

  char *full_filepath = realpath(filepath, NULL);
  if (full_filepath == NULL && errno == ENOENT) {
    /* The given filename doesn't exist in the current directory.
       Likely, this is the case because the user only gave a filename
       from the source directory, not the proper relative filepath.
       We work around this by only comparing file names. */
    char *filepath_cpy = strdup(filepath);
    char *expect_file_name = basename(filepath_cpy);

    bool equal_names = strcmp(expect_file_name, file_name) == 0;

    free(filepath_cpy);
    free(die_filepath);

    return equal_names;
  } else {
    char *expect_file_name = basename(full_filepath);
    char *expect_dir_name = dirname(full_filepath);

    bool equal_names = strcmp(file_name, expect_file_name) == 0;
    bool equal_dirs = strcmp(dir_name, expect_dir_name) == 0;

    free(full_filepath);
    free(die_filepath);

    return equal_names && equal_dirs;
  }
}

typedef struct {
  size_t nalloc;
  size_t idx;
  char **filepaths;
} Filepaths;

enum { FILEPATHS_ALLOC=8 };

bool callback__get_filepaths(
  Dwarf_Debug dbg, Dwarf_Die cu_die,
  const void *const search_for, void *const search_findings
) {  
  assert(search_findings != NULL);
  unused(search_for);


  if (sd_has_tag(dbg, cu_die, DW_TAG_compile_unit)) {
    Filepaths *filepaths = (Filepaths *) search_findings;

    char *this_filepath = sd_get_filepath(dbg, cu_die);  
    /* Important: if `this_filepath` is `NULL` and is still
       stored in the array, then all subsequent strings will
       be leaked later on. */
    if (this_filepath != NULL) {
      if (filepaths->idx >= filepaths->nalloc) {
        filepaths->nalloc += FILEPATHS_ALLOC;
        filepaths->filepaths = (char **) realloc (filepaths->filepaths,
                                                  sizeof(char  *) * filepaths->nalloc);
        assert(filepaths->filepaths != NULL);
      }
      filepaths->filepaths[filepaths->idx] = this_filepath;
      filepaths->idx ++;
    }
  }

  /* Never signal success so as to walk all CU DIEs. */
  return false;
}

/* Return a NULL-terminated array of all filepaths. */
char **sd_get_filepaths(Dwarf_Debug dbg) {
  assert(dbg != NULL);  

  Dwarf_Error error = NULL;

  Filepaths filepaths = {
    .nalloc = 0,
    .idx = 0,
    .filepaths = NULL,
  };

  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__get_filepaths,
    NULL, &filepaths);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
    return NULL;
  } else {  /* We expect DW_DLV_NO_ENTRY here. */
    char **filepaths_arr = (char **) realloc (filepaths.filepaths,
                                              (filepaths.idx + 1) * sizeof(char *));
    assert(filepaths_arr != NULL);
    filepaths_arr[filepaths.idx] = NULL;
    return filepaths_arr;
  }
}

bool callback__get_srclines(
  Dwarf_Debug dbg, Dwarf_Die cu_die,
  const void *const search_for, void *const search_findings
) {
  assert(search_for != NULL);
  assert(search_findings != NULL);

  const char *filepath = (const char *) search_for;
  LineTable *line_table = (LineTable *) search_findings;
  
  int res = DW_DLV_OK;
  Dwarf_Error error = NULL;

  /* Is this DIE a CU DIE? */
  if (!sd_has_tag(dbg, cu_die, DW_TAG_compile_unit)) {
    return false;
  }

  /* Is this CU DIE from the correct file? */
  if (!sd_is_die_from_file(dbg, cu_die, filepath)) {
    return false;
  }

  Dwarf_Line_Context line_context = NULL;
  if (!sd_get_line_context(dbg, cu_die, &line_context)) {
    return false;
  }

  Dwarf_Line *lines = NULL;
  Dwarf_Signed n_lines = 0;

  res = dwarf_srclines_from_linecontext(line_context,
    &lines, &n_lines,
    &error);

  if (res != DW_DLV_OK) {
    dwarf_srclines_dealloc_b(line_context);
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return false;
  } else {
    LineEntry *line_entries = (LineEntry *) calloc (n_lines, sizeof(LineEntry));
    for (unsigned i = 0; i < n_lines; i++) {
      res = sd_line_entry_from_dwarf_line(lines[i],
                                          &line_entries[i],
                                          &error);
      if (res != DW_DLV_OK) {
        if (res == DW_DLV_ERROR) {
          dwarf_dealloc_error(dbg, error);
        }
        free(line_entries);
        return false;
      }

      /* Must be the case if `res != DW_DLV_OK` */
      assert(line_entries[i].is_ok);
    }

    line_table->lines        = line_entries;
    line_table->n_lines      = n_lines;
    line_table->line_context = line_context;
    line_table->is_set       = true;
    return true;
  }
}

LineTable sd_get_line_table(Dwarf_Debug dbg, const char *filepath) {
  assert(dbg != NULL);
  assert(filepath != NULL);

  Dwarf_Error error = NULL;

  LineTable line_table = { 0 };

  int res = sd_search_dwarf_dbg(dbg, &error,
    callback__get_srclines,
    filepath, &line_table);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return (LineTable) { .is_set=false };
  } else {
    return line_table;
  }
}

/* Set `index_dest` to the line that
   contains the address of `PC`. */
SprayResult get_line_table_index_of_pc(const LineTable line_table,
                                      x86_addr pc,
                                      unsigned *index_dest
) {
  assert(index_dest != NULL);

  unsigned i = 0;
  while (
    i < line_table.n_lines &&
    line_table.lines[i].addr.value < pc.value
  ) i++;

  if (
    i < line_table.n_lines &&
    line_table.lines[i].addr.value >= pc.value &&
    (
      i + 1 >= line_table.n_lines ||
      pc.value < line_table.lines[i + 1].addr.value
    )
   ) {
    *index_dest = i;
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

/* Set `index_dest` to the line with the line number `lineno`. */
SprayResult get_line_table_index_of_line(const LineTable line_table,
                                         unsigned lineno,
                                         unsigned *index_dest
) {
  assert(index_dest != NULL);

  unsigned i = 0;
  while (
    i < line_table.n_lines &&
    line_table.lines[i].ln < lineno
  ) i++;

  if (
    i < line_table.n_lines &&
    line_table.lines[i].ln >= lineno
  ) {
    *index_dest = i;
    return SP_OK;
  } else {
    return SP_ERR;
  }
}

LineEntry get_line_entry_from_pc(Dwarf_Debug dbg, x86_addr pc) {
  assert(dbg != NULL);

  Dwarf_Addr pc_addr = pc.value;

  char *filepath = get_filepath_from_pc(dbg, pc);  
  if (filepath == NULL) {
    return (LineEntry) { .is_ok=false };
  }

  LineTable line_table = sd_get_line_table(dbg, filepath);
  free(filepath);
  if (!line_table.is_set) {
    return (LineEntry) { .is_ok=false };
  }

  for (unsigned i = 0; i < line_table.n_lines; i++) {
    if (line_table.lines[i].addr.value == pc_addr) {
      LineEntry ret = line_table.lines[i];
      sd_free_line_table(&line_table);
      return ret;
    }
  }

  unsigned pc_line_idx = 0;
  SprayResult res = get_line_table_index_of_pc(line_table, pc, &pc_line_idx);

  if (res == SP_ERR) {
    sd_free_line_table(&line_table);
    return (LineEntry) { .is_ok=false };
  } else {
    LineEntry ret =  line_table.lines[pc_line_idx];
    sd_free_line_table(&line_table);
    return ret;
  }
}

LineEntry get_line_entry_from_pc_exact(Dwarf_Debug dbg, x86_addr pc) {
  assert(dbg != NULL);

  LineEntry line_entry = get_line_entry_from_pc(dbg, pc);
  if (line_entry.is_ok && line_entry.addr.value == pc.value) {
    return line_entry;    
  } else {
    return (LineEntry) { .is_ok=false };
  }
}

LineEntry get_line_entry_at(Dwarf_Debug dbg, const char *filepath, unsigned lineno) {
  assert(dbg != NULL);
  assert(filepath != NULL);

  LineTable line_table = sd_get_line_table(dbg, filepath);
  if (!line_table.is_set) {
    return (LineEntry) { .is_ok=false };
  }

  unsigned line_idx = 0;
  SprayResult res = get_line_table_index_of_line(line_table, lineno, &line_idx);
  if (res == SP_ERR) {
    sd_free_line_table(&line_table);
    return (LineEntry) { .is_ok=false };
  } else {
    LineEntry ret =  line_table.lines[line_idx];
    sd_free_line_table(&line_table);
    return ret;    
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
bool callback__find_subprog_attr_by_subprog_name(Dwarf_Debug dbg, Dwarf_Die die,
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
    callback__find_subprog_attr_by_subprog_name,
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

SprayResult for_each_line_in_subprog(
  Dwarf_Debug dbg,
  const char *fn_name,
  const char *filepath,
  LineCallback callback,
  void *const init_data
) {
  assert(dbg != NULL);
  assert(fn_name != NULL);
  assert(filepath != NULL);
  assert(callback != NULL);
  assert(init_data != NULL);

  SubprogAttr attr = sd_get_subprog_attr(dbg, fn_name);
  if (!attr.is_set) {
    return SP_ERR;
  }

  LineTable line_table = sd_get_line_table(dbg, filepath);
  if (!line_table.is_set) {
    return SP_ERR;
  }

  unsigned i = 0;
  SprayResult res = get_line_table_index_of_pc(line_table,
                                               attr.lowpc,
                                               &i);
  if (res == SP_ERR) {
    sd_free_line_table(&line_table);
    return SP_ERR;
  }

  /* Run the callback on the lines inside the function. */
  for (;
    i < line_table.n_lines &&
    line_table.lines[i].addr.value <= attr.highpc.value;
    i++
  ) {
    if (line_table.lines[i].new_statement) {
      SprayResult res = callback(&line_table.lines[i], init_data);
      if (res == SP_ERR) {
        sd_free_line_table(&line_table);
        return SP_ERR;
      }
    }
  }

  sd_free_line_table(&line_table);

  return SP_OK;
}

SprayResult get_effective_start_addr(Dwarf_Debug dbg, x86_addr prologue_start,
                                     x86_addr function_end,
                                     x86_addr *function_start) {
  assert(dbg != NULL);
  assert(function_start != NULL);

  char **filepaths = sd_get_filepaths(dbg);
  for (size_t j = 0; filepaths[j] != NULL; j++) {
    LineTable line_table = sd_get_line_table(dbg, filepaths[j]);
    free(filepaths[j]);

    if (!line_table.is_set) {
      continue;
    } else { 
      unsigned first_line = 0;
      SprayResult res =
          get_line_table_index_of_pc(line_table, prologue_start, &first_line);
      if (res == SP_ERR) {
        sd_free_line_table(&line_table);
        continue;
      } else {  /* We've found the correct function. */
        for (size_t k = j + 1; filepaths[k] != NULL; k++) {
          free(filepaths[k]);
        }
        free(filepaths);

        /* Either find the prologue end line or pick the first
           line after the line of the low PC as the start. */
        for (unsigned i = first_line;
             i < line_table.n_lines &&
             line_table.lines[i].addr.value <= function_end.value;
             i++) {
          if (line_table.lines[i].prologue_end) {
            *function_start = line_table.lines[i].addr;
            sd_free_line_table(&line_table);
            return SP_OK;
          }
        }

        /* None of the line entries had `prologue_end` set. */

        if (first_line + 1 < line_table.n_lines) {
          *function_start = line_table.lines[first_line + 1].addr;
        } else {
          *function_start = line_table.lines[first_line].addr;
        }
        return SP_OK;
      }
    }
  }

  return SP_ERR;
}
