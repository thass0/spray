#include "spray_dwarf.h"

#include "magic.h"
#include "registers.h"		/* For evaluating location expressions. */

#include <dwarf.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>		/* For `log10`, `fabs` and `floor` to compute the number of digits. */


/* NOTE: The function prefix `sd_` stands for 'Spray DWARF' and
   is used for all of  Spray's functions that interface with
   DWARF debug information (through libdwarf). */

#ifndef UNIT_TESTS
typedef struct SearchFor {
  unsigned level;   /* Level in the DIE tree. */
  const void *data; /* Custom data used as context while searching. */
} SearchFor;

typedef struct SearchFindings {
  void *data;			/* Custom data collected while searching */
} SearchFindings;

/* This type is defined in `spray_dwarf.h` if `UNIT_TESTS` is defined. */
typedef bool (*SearchCallback)(Dwarf_Debug, Dwarf_Die, SearchFor, SearchFindings);
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

Dwarf_Debug sd_dwarf_init(const char *restrict filepath, Dwarf_Error *error) {
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

  int res = dwarf_init_path(filepath,  /* Only external parameter. */
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

/* NOTE: The return values of `search_dwarf_die` and `search_dwarf_dbg`
   only signals the outcome of the libdwarf calls. Therefore the return
   value `DW_DLV_OK` doesn't mean that the search was successful in that
   we found something. It only means that there wasn't a error with DWARF.
   `search_findings` should be used to signal the outcome of the search instead.
   On the other hand, if the return value is not `DW_DLV_OK`, the search was
   never successful. E.g. if nothing was found, `DW_DLV_NO_ENTRY` is returned. */

int sd_search_dwarf_die(Dwarf_Debug dbg,
			Dwarf_Die in_die,
			Dwarf_Error *const error,
			int is_info,
			unsigned level,
			SearchCallback search_callback,
			const void *search_for_data,
			void  *search_findings_data
) {  
  int res = DW_DLV_OK;
  Dwarf_Die cur_die = in_die;
  Dwarf_Die child_die = NULL;

  SearchFor search_for = {
    .level = level,		/* Current level of recursion. */
    .data = search_for_data,
  };
  SearchFindings search_findings = {
    .data = search_findings_data,
  };

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
      /* We found a child. Go down another level of recursion! */
      res = sd_search_dwarf_die(dbg,
				child_die,
				error,
				is_info,
				level + 1,
				search_callback,
				search_for_data,
				search_findings_data);
  
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

/* Search the `Dwarf_Debug` instance. For each DIE `search_callback` is called
   and passed instances of `SearchFor` and `SearchFindings`. Any time `search_callback`
   returns `true`, the search ends. If it returns `false`, the search goes on. */
int sd_search_dwarf_dbg(Dwarf_Debug dbg,
			Dwarf_Error *const error,
			SearchCallback search_callback,
			const void *search_for_data,
			void *search_findings_data) {
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

    res = sd_search_dwarf_die(dbg,
			      cu_die,
			      error,
			      is_info,
			      0,
			      search_callback,
			      search_for_data,
			      search_findings_data);

    dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
    cu_die = NULL;

    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_OK) {
      search_finished = true;
    }
  }
}

int sd_get_high_and_low_pc(Dwarf_Die die,
			   Dwarf_Error *error,
			   Dwarf_Addr *lowpc,
			   Dwarf_Addr *highpc) {
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

/* Sets `pc_in_die` to `true` if it finds that `pc` lies
   between `DW_AT_pc_low` and `DW_AT_pc_high` of the DIE.

   The libdwarf error code of the internal calls is returned.

   `pc_in_die` stays untouched on error. */
int sd_check_pc_in_die(Dwarf_Die die, Dwarf_Error *const error, Dwarf_Addr pc, bool *pc_in_die) {
  int res = DW_DLV_OK;

  Dwarf_Addr lowpc, highpc;
  res = sd_get_high_and_low_pc(die, error, &lowpc, &highpc);

  if (res != DW_DLV_OK) {
    return res;
  } else if (lowpc <= pc && pc <= highpc) {
    *pc_in_die = true;
    return DW_DLV_OK;
  } else {
    /* `pc` is not in the DIE's range. */
    *pc_in_die = false;
    return DW_DLV_OK;
  }
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

/* Used to find the name of the subprogram that contains a given PC.
   Not in use right now. See `spray_elf.h` for the same functionality. */
bool callback__find_subprog_name_by_pc(Dwarf_Debug dbg,
				       Dwarf_Die die,
				       SearchFor search_for,
				       SearchFindings search_findings) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for.data;
  char **fn_name = (char **) search_findings.data;

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
        *fn_name = realloc(*fn_name, len + 1);
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

/* Get the name of the subprogram that contains the given PC.

   On success, a heap-allocated string is returned that should
   be `free`d.

   `NULL` is returned if there was an error, or if no subprogram
   was found that contains the PC. */
/* char *sd_get_subprog_name_from_pc(Dwarf_Debug dbg, dbg_addr _pc) { */
/*   assert(dbg != NULL); */

/*   Dwarf_Addr pc = _pc.value; */
/*   char *fn_name = NULL; */

/*   Dwarf_Error error = NULL; */

/*   int res = sd_search_dwarf_dbg(dbg, */
/* 				&error, */
/* 				callback__find_subprog_name_by_pc, */
/* 				&pc, */
/* 				&fn_name); */

/*   if (res != DW_DLV_OK) { */
/*     if (res == DW_DLV_ERROR) { */
/*       dwarf_dealloc_error(dbg, error); */
/*     } */
/*     return NULL; */
/*   } else { */
/*     return fn_name; */
/*   } */
/* } */

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

int sd_line_entry_from_dwarf_line(Dwarf_Line line, LineEntry *line_entry,
                                  Dwarf_Error *error) {
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
  line_entry->is_exact =
      false; // `false` by default. Might be set by `sd_line_entry_from_pc`.
  line_entry->ln = lineno;
  line_entry->cl = colno;
  line_entry->addr = (dbg_addr) { addr };
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
    /* Get file and directory names for CU DIE. */
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

bool callback__find_filepath_by_pc(Dwarf_Debug dbg,
				   Dwarf_Die die,
				   SearchFor search_for,
				   SearchFindings search_findings) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for.data;
  char **filepath = (char **) search_findings.data;

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

char *sd_filepath_from_pc(Dwarf_Debug dbg, dbg_addr pc) {
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

/* Check to see if the given attribute has a DWARF form
   that can be used with `dwarf_get_loclist_c`. The libdwarf
   docs for `dwarf_get_loclist_c` describe this procedure. */
SprayResult sd_init_loc_attr(Dwarf_Debug dbg,
			     Dwarf_Die die,
			     Dwarf_Attribute attr,
			     SdLocAttr *attr_dest) {
  int res = DW_DLV_OK;
  Dwarf_Half version = 0;
  Dwarf_Half offset_size = 0;

  res = dwarf_get_version_of_die(die, &version, &offset_size);
  if (res != DW_DLV_OK) {
    return SP_ERR;
  } else {
    Dwarf_Error error = NULL;

    Dwarf_Half form = 0;
    res = dwarf_whatform(attr, &form, &error);
    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
	dwarf_dealloc_error(dbg, error);
      }
      return SP_ERR;
    }

    Dwarf_Half num = 0;
    res = dwarf_whatattr(attr, &num, &error);
    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
	dwarf_dealloc_error(dbg, error);
      }
      return SP_ERR;
    }

    enum Dwarf_Form_Class class = dwarf_get_form_class(version,
						       num,
						       offset_size,
						       form);
    if (class == DW_FORM_CLASS_EXPRLOC
	|| class == DW_FORM_CLASS_LOCLIST
	|| class == DW_FORM_CLASS_LOCLISTSPTR
	|| class == DW_FORM_CLASS_BLOCK) {
      *attr_dest = (SdLocAttr) {
	.attr=attr
      };
      return SP_OK;
    } else {
      return SP_ERR;
    }
  }
}

typedef struct LocAttrSearchFor {
  const char *name;
  Dwarf_Half attr_num;
} LocAttrSearchFor;

bool callback_find_subprog_loc_attr_by_subprog_name(Dwarf_Debug dbg,
						    Dwarf_Die die,
						    SearchFor search_for,
						    SearchFindings search_findings) {
  assert(dbg != NULL);

  const LocAttrSearchFor *_search_for = search_for.data;
  const char *subprog_name = _search_for->name;
  Dwarf_Half attr_num = _search_for->attr_num;

  if (sd_is_subprog_with_name(dbg, die, subprog_name)) {
    int res = 0;
    Dwarf_Error error;

    Dwarf_Attribute attr = NULL;
    res = dwarf_attr(die, attr_num, &attr, &error);

    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
	dwarf_dealloc_error(dbg, error);
      }
      return false;
    } else {
      SdLocAttr *loc_attr = search_findings.data;
      SprayResult res = sd_init_loc_attr(dbg,
					 die,
					 attr,
					 loc_attr);
      if (res == SP_OK) {
	return true;
      } else {
	return false;
      }
    }
  } else {
    /* Not the right kind (`DW_TAG`) of DIE. */
    return false;
  }
}

/* Find any location attribute (`attr_num`) of a subprogram DIE with given name.

   On success the location attribute is stored in `loc_dest` and `SP_OK` is returned.

   `SP_ERR` is returned on error and `loc_dest` stays untouched. */
SprayResult sd_get_subprog_loc_attr(Dwarf_Debug dbg,
				    const char *subprog_name,
				    Dwarf_Half attr_num,
				    SdLocAttr *loc_dest) {
  assert(dbg != NULL);
  assert(subprog_name != NULL);

  Dwarf_Error error = NULL;

  LocAttrSearchFor search_for = {
    .name = subprog_name,
    .attr_num = attr_num,
  };
  SdLocAttr loc_attr = {NULL};

  int res = sd_search_dwarf_dbg(dbg,
				&error,
				callback_find_subprog_loc_attr_by_subprog_name,
				&search_for,
				&loc_attr);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return SP_ERR;
  } else {
    *loc_dest = loc_attr;
    return SP_OK;
  }
}

/* While traversing the DIE tree, we use `in_scope` in `VarLocSearchFindings` to
   keep track of whether the most recent `DW_TAG_subprogram` DIE contained the
   PC we are looking for.
   Once we find another DIE with a PC range, we update the search
   findings depending on whether or not the PC is still in this range.
   Only if the given PC is currently in the range of the DIEs, do we consider
   looking for the variable or formal parameter. Otherwise, we are not in the
   right scope. */

typedef struct VarLocSearchFor {
  dbg_addr pc;
  bool use_scope;		/* Don't try to find the variable in the scope
				   of `func_name` if this is set to `false`. */
  const char *var_name;
} VarLocSearchFor;

typedef struct VarLocSearchFindings {
  bool in_scope;
  unsigned scope_level;
  SdLocAttr attr;
} VarLocSearchFindings;

/* Search callback used in combination with `sd_search_dwarf_die` that
   retrieves the location attribute of a `DW_TAG_variable` DIE with a
   given variable name. */
bool callback__find_location_by_variable_name(Dwarf_Debug dbg,
					      Dwarf_Die die,
					      SearchFor search_for,
					      SearchFindings search_findings) {
  int res = DW_DLV_OK;
  Dwarf_Error error = NULL;

  VarLocSearchFor *var_search_for = (VarLocSearchFor *) search_for.data;
  VarLocSearchFindings *var_search_findings = (VarLocSearchFindings *) search_findings.data;

  /* Do we need to keep track of the current scope? */
  if (var_search_for->use_scope) {

    if (!(search_for.level > var_search_findings->scope_level)) {
      /* Any scope is only active as long as the current level is deeper
	 (higher value) than that of the scope's subprogram DIE. Otherwise,
	 the current DIE is either above or next to the scope. */
      var_search_findings->in_scope = false;      
    }

    /* Update the active scope, if this DIE is a subprogram. */
    if (sd_has_tag(dbg, die, DW_TAG_subprogram)) {
      Dwarf_Addr pc = var_search_for->pc.value;

      res = sd_check_pc_in_die(die,
			       &error,
			       pc,
			       &var_search_findings->in_scope);
      if (res == DW_DLV_OK) {
	if (var_search_findings->in_scope) {
	  /* Update the level only if the scope is active. */
	  var_search_findings->scope_level = search_for.level;	  
	}
      } else {
	if (res == DW_DLV_ERROR) {
	  dwarf_dealloc_error(dbg, error);
	}
      }

      /* A subprogram DIE won't be a variable, so just return. */
      return false;
    }    
  }

  /* If the scope is used we must be in scope to look for a variable.
     Otherwise, if the scope is ignored, it doesn't matter if we're in scope. */
  if (!var_search_for->use_scope
      || (var_search_for->use_scope && var_search_findings->in_scope)) {

    bool correct_tag = sd_has_tag(dbg, die, DW_TAG_variable)
      || sd_has_tag(dbg, die, DW_TAG_formal_parameter);

    bool location_attr = sd_has_at(dbg, die, DW_AT_location);

    /* Is the given DIE a `DW_TAG_variable` and does it contain the `DW_AT_location` attribute? */
    if (correct_tag && location_attr) {
      const char *var_name = var_search_for->var_name;

      int res = DW_DLV_OK;
      Dwarf_Error error = NULL;

      char *die_var_name = NULL;  /* Don't free the string returned by `dwarf_diename`. */
      res = dwarf_diename(die, &die_var_name, &error);

      if (res == DW_DLV_OK && strcmp(var_name, die_var_name) == 0) {
	/* Retrieve the `location` attribute of this DIE. */
	Dwarf_Attribute attr = NULL;
	res = dwarf_attr(die, DW_AT_location, &attr, &error);

	if (res != DW_DLV_OK) {
	  if (res == DW_DLV_ERROR) {
	    dwarf_dealloc_error(dbg, error);
	  }
	  return false;
	} else {
	  /* Ensure that the attribute we found has the right
	     form, too. This should never fail for the `location`
	     attribute of `variable` and `formal_parameter` DIEs. */
	  SprayResult res = sd_init_loc_attr(dbg,
					     die,
					     attr,
					     &var_search_findings->attr);
	  if (res == SP_OK) {
	    return true;
	  } else {
	    return false;
	  }
	}	      
      } else {
	if (res == DW_DLV_ERROR) {
	  dwarf_dealloc_error(dbg, error);
	}
	/* This is a `DW_TAG_variable` DIE but it
	   doesn't have the given variable name. */
	return false;      
      }
    } else {
      /* DIE was wrong tag or wrong attributes. */
      return false;
    }
  } else {
    /* Not in scope. */
    return false;
  }
}

/* Return the `DW_AT_location` attribute from the
   DIE of the variable with the given name. */
SprayResult sd_location_from_variable_name(Dwarf_Debug dbg,
					   dbg_addr pc,
					   const char *var_name,
					   SdLocAttr *attr) {
  assert(dbg != NULL);
  assert(var_name != NULL);
  Dwarf_Error error = NULL;

  VarLocSearchFor search_for = {
    .pc = pc,
    .var_name = var_name,
    .use_scope = true,
  };

  VarLocSearchFindings search_findings = {
    .in_scope = false,
    .attr = {0},
  };

  int res = sd_search_dwarf_dbg(dbg,
				&error,
				callback__find_location_by_variable_name,
				&search_for,
				&search_findings);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
    return SP_ERR;
  } else if (res == DW_DLV_NO_ENTRY) {
    /* Try again, this time ignoring the scope. Thereby, the first
       global variable with the given name is chosen. */
    search_for.use_scope = false;
    res = sd_search_dwarf_dbg(dbg,
			      &error,
			      callback__find_location_by_variable_name,
			      &search_for,
			      &search_findings);

    if (res != DW_DLV_OK) {
      if (res == DW_DLV_ERROR) {
	dwarf_dealloc_error(dbg, error);
      }
      return SP_ERR;
    } else {
      *attr = search_findings.attr;
      return SP_OK;
    }
  } else {
    *attr = search_findings.attr;
    return SP_OK;
  }
}

#ifndef UNIT_TESTS

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

/* Initialize an `SdExpression` from the given location description entry.
   Returns a regular libdwarf error code. `error` must be handled if a libdwarf
   error is returned. `expr_dest` is only changed on success. */
int sd_init_loc_expression(Dwarf_Locdesc_c locdesc_entry,
			   size_t locexpr_op_count,
			   SdExpression *expr_dest,
			   Dwarf_Error *error) {
  assert(locdesc_entry != NULL);
  assert(expr_dest != NULL);
  assert(error != NULL);

  int res = DW_DLV_OK;

  size_t n_operations = locexpr_op_count;
  SdOperation *operations = calloc(n_operations, sizeof(SdOperation));
  assert(operations != NULL);

  for (size_t i = 0; i < n_operations; i++) {
    /* Byte offset of the operator in the entire expression. It's
       suggested to use this to validate the correctness of branching
       operations. I image that you'd check if this value reflects the
       expected offset after performing a branch. */
    Dwarf_Unsigned offset_for_branch = 0;
    SdOperation *op = operations + i;

    res = dwarf_get_location_op_value_c(locdesc_entry,
					i,
					&op->opcode,
					&op->operand1,
					&op->operand2,
					&op->operand3,
					&offset_for_branch,
					error);
    if (res != DW_DLV_OK) {
      free(operations);
      return res;
    }
  }

  /* All location expressions were read successfully. */
  expr_dest->n_operations = n_operations;
  expr_dest->operations = operations;

  return DW_DLV_OK;
}

/* Free the memory associated with the given DWARF expression. */
void del_expression(SdExpression *expr) {
  if (expr != NULL) {
    free(expr->operations);
    expr->operations = NULL;
    expr->n_operations = 0;
  }
}

/* Return a string describing the given DWARF expression opcode. */
const char *sd_what_operator(SdOperator opcode) {
  static const char *operator_names[256] = {
    [DW_OP_addr] = "DW_OP_addr",		/* Constant address.  */
    [DW_OP_deref] = "DW_OP_deref",
    [DW_OP_const1u] = "DW_OP_const1u",	/* Unsigned 1-byte constant.  */
    [DW_OP_const1s] = "DW_OP_const1s",	/* Signed 1-byte constant.  */
    [DW_OP_const2u] = "DW_OP_const2u",	/* Unsigned 2-byte constant.  */
    [DW_OP_const2s] = "DW_OP_const2s",	/* Signed 2-byte constant.  */
    [DW_OP_const4u] = "DW_OP_const4u",	/* Unsigned 4-byte constant.  */
    [DW_OP_const4s] = "DW_OP_const4s",	/* Signed 4-byte constant.  */
    [DW_OP_const8u] = "DW_OP_const4u",	/* Unsigned 8-byte constant.  */
    [DW_OP_const8s] = "DW_OP_const8s",	/* Signed 8-byte constant.  */
    [DW_OP_constu] = "DW_OP_constu",	/* Unsigned LEB128 constant.  */
    [DW_OP_consts] = "DW_OP_consts",	/* Signed LEB128 constant.  */
    [DW_OP_dup] = "DW_OP_dup",
    [DW_OP_drop] = "DW_OP_drop",
    [DW_OP_over] = "DW_OP_over",
    [DW_OP_pick] = "DW_OP_pick",		/* 1-byte stack index.  */
    [DW_OP_swap] = "DW_OP_swap",
    [DW_OP_rot] = "DW_OP_rot",
    [DW_OP_xderef] = "DW_OP_xderef",
    [DW_OP_abs] = "DW_OP_abs",
    [DW_OP_and] = "DW_OP_and",
    [DW_OP_div] = "DW_OP_div",
    [DW_OP_minus] = "DW_OP_minus",
    [DW_OP_mod] = "DW_OP_mod",
    [DW_OP_mul] = "DW_OP_mul",
    [DW_OP_neg] = "DW_OP_neg",
    [DW_OP_not] = "DW_OP_not",
    [DW_OP_or] = "DW_OP_or",
    [DW_OP_plus] = "DW_OP_plus",
    [DW_OP_plus_uconst] = "DW_OP_plus_uconst",	/* Unsigned LEB128 addend.  */
    [DW_OP_shl] = "DW_OP_shl",
    [DW_OP_shr] = "DW_OP_shr",
    [DW_OP_shra] = "DW_OP_shra",
    [DW_OP_xor] = "DW_OP_xor",
    [DW_OP_bra] = "DW_OP_bra",		/* Signed 2-byte constant.  */
    [DW_OP_eq] = "DW_OP_eq",
    [DW_OP_ge] = "DW_OP_ge",
    [DW_OP_gt] = "DW_OP_gt",
    [DW_OP_le] = "DW_OP_le",
    [DW_OP_lt] = "DW_OP_lt",
    [DW_OP_ne] = "DW_OP_ne",
    [DW_OP_skip] = "DW_OP_skip",		/* Signed 2-byte constant.  */
    [DW_OP_lit0] = "DW_OP_lit0",		/* Literal 0.  */
    [DW_OP_lit1] = "DW_OP_lit1",		/* Literal 1.  */
    [DW_OP_lit2] = "DW_OP_lit2",		/* Literal 2.  */
    [DW_OP_lit3] = "DW_OP_lit3",		/* Literal 3.  */
    [DW_OP_lit4] = "DW_OP_lit4",		/* Literal 4.  */
    [DW_OP_lit5] = "DW_OP_lit5",		/* Literal 5.  */
    [DW_OP_lit6] = "DW_OP_lit6",		/* Literal 6.  */
    [DW_OP_lit7] = "DW_OP_lit7",		/* Literal 7.  */
    [DW_OP_lit8] = "DW_OP_lit8",		/* Literal 8.  */
    [DW_OP_lit9] = "DW_OP_lit9",		/* Literal 9.  */
    [DW_OP_lit10] = "DW_OP_lit_10",		/* Literal 10.  */
    [DW_OP_lit11] = "DW_OP_lit11",		/* Literal 11.  */
    [DW_OP_lit12] = "DW_OP_lit12",		/* Literal 12.  */
    [DW_OP_lit13] = "DW_OP_lit13",		/* Literal 13.  */
    [DW_OP_lit14] = "DW_OP_lit14",		/* Literal 14.  */
    [DW_OP_lit15] = "DW_OP_lit15",		/* Literal 15.  */
    [DW_OP_lit16] = "DW_OP_lit16",		/* Literal 16.  */
    [DW_OP_lit17] = "DW_OP_lit17",		/* Literal 17.  */
    [DW_OP_lit18] = "DW_OP_lit18",		/* Literal 18.  */
    [DW_OP_lit19] = "DW_OP_lit19",		/* Literal 19.  */
    [DW_OP_lit20] = "DW_OP_lit20",		/* Literal 20.  */
    [DW_OP_lit21] = "DW_OP_lit21",		/* Literal 21.  */
    [DW_OP_lit22] = "DW_OP_lit22",		/* Literal 22.  */
    [DW_OP_lit23] = "DW_OP_lit23",		/* Literal 23.  */
    [DW_OP_lit24] = "DW_OP_lit24",		/* Literal 24.  */
    [DW_OP_lit25] = "DW_OP_lit25",		/* Literal 25.  */
    [DW_OP_lit26] = "DW_OP_lit26",		/* Literal 26.  */
    [DW_OP_lit27] = "DW_OP_lit27",		/* Literal 27.  */
    [DW_OP_lit28] = "DW_OP_lit28",		/* Literal 28.  */
    [DW_OP_lit29] = "DW_OP_lit29",		/* Literal 29.  */
    [DW_OP_lit30] = "DW_OP_lit30",		/* Literal 30.  */
    [DW_OP_lit31] = "DW_OP_lit31",		/* Literal 31.  */
    [DW_OP_reg0] = "DW_OP_reg0",		/* Register 0.  */
    [DW_OP_reg1] = "DW_OP_reg1",		/* Register 1.  */
    [DW_OP_reg2] = "DW_OP_reg2",		/* Register 2.  */
    [DW_OP_reg3] = "DW_OP_reg3",		/* Register 3.  */
    [DW_OP_reg4] = "DW_OP_reg4",		/* Register 4.  */
    [DW_OP_reg5] = "DW_OP_reg5",		/* Register 5.  */
    [DW_OP_reg6] = "DW_OP_reg6",		/* Register 6.  */
    [DW_OP_reg7] = "DW_OP_reg7",		/* Register 7.  */
    [DW_OP_reg8] = "DW_OP_reg8",		/* Register 8.  */
    [DW_OP_reg9] = "DW_OP_reg9",		/* Register 9.  */
    [DW_OP_reg10] = "DW_OP_reg10",		/* Register 10.  */
    [DW_OP_reg11] = "DW_OP_reg11",		/* Register 11.  */
    [DW_OP_reg12] = "DW_OP_reg12",		/* Register 12.  */
    [DW_OP_reg13] = "DW_OP_reg13",		/* Register 13.  */
    [DW_OP_reg14] = "DW_OP_reg14",		/* Register 14.  */
    [DW_OP_reg15] = "DW_OP_reg15",		/* Register 15.  */
    [DW_OP_reg16] = "DW_OP_reg16",		/* Register 16.  */
    [DW_OP_reg17] = "DW_OP_reg17",		/* Register 17.  */
    [DW_OP_reg18] = "DW_OP_reg18",		/* Register 18.  */
    [DW_OP_reg19] = "DW_OP_reg19",		/* Register 19.  */
    [DW_OP_reg20] = "DW_OP_reg20",		/* Register 20.  */
    [DW_OP_reg21] = "DW_OP_reg21",		/* Register 21.  */
    [DW_OP_reg22] = "DW_OP_reg22",		/* Register 22.  */
    [DW_OP_reg23] = "DW_OP_reg23",		/* Register 24.  */
    [DW_OP_reg24] = "DW_OP_reg24",		/* Register 24.  */
    [DW_OP_reg25] = "DW_OP_reg25",		/* Register 25.  */
    [DW_OP_reg26] = "DW_OP_reg26",		/* Register 26.  */
    [DW_OP_reg27] = "DW_OP_reg27",		/* Register 27.  */
    [DW_OP_reg28] = "DW_OP_reg28",		/* Register 28.  */
    [DW_OP_reg29] = "DW_OP_reg29",		/* Register 29.  */
    [DW_OP_reg30] = "DW_OP_reg30",		/* Register 30.  */
    [DW_OP_reg31] = "DW_OP_reg31",		/* Register 31.  */
    [DW_OP_breg0] = "DW_OP_breg0",		/* Base register 0.  */
    [DW_OP_breg1] = "DW_OP_breg1",		/* Base register 1.  */
    [DW_OP_breg2] = "DW_OP_breg2",		/* Base register 2.  */
    [DW_OP_breg3] = "DW_OP_breg3",		/* Base register 3.  */
    [DW_OP_breg4] = "DW_OP_breg4",		/* Base register 4.  */
    [DW_OP_breg5] = "DW_OP_breg5",		/* Base register 5.  */
    [DW_OP_breg6] = "DW_OP_breg6",		/* Base register 6.  */
    [DW_OP_breg7] = "DW_OP_breg7",		/* Base register 7.  */
    [DW_OP_breg8] = "DW_OP_breg8",		/* Base register 8.  */
    [DW_OP_breg9] = "DW_OP_breg9",		/* Base register 9.  */
    [DW_OP_breg10] = "DW_OP_breg10",	/* Base register 10.  */
    [DW_OP_breg11] = "DW_OP_breg11",	/* Base register 11.  */
    [DW_OP_breg12] = "DW_OP_breg12",	/* Base register 12.  */
    [DW_OP_breg13] = "DW_OP_breg13",	/* Base register 13.  */
    [DW_OP_breg14] = "DW_OP_breg14",	/* Base register 14.  */
    [DW_OP_breg15] = "DW_OP_breg15",	/* Base register 15.  */
    [DW_OP_breg16] = "DW_OP_breg16",	/* Base register 16.  */
    [DW_OP_breg17] = "DW_OP_breg17",	/* Base register 17.  */
    [DW_OP_breg18] = "DW_OP_breg18",	/* Base register 18.  */
    [DW_OP_breg19] = "DW_OP_breg19",	/* Base register 19.  */
    [DW_OP_breg20] = "DW_OP_breg20",	/* Base register 20.  */
    [DW_OP_breg21] = "DW_OP_breg21",	/* Base register 21.  */
    [DW_OP_breg22] = "DW_OP_breg22",	/* Base register 22.  */
    [DW_OP_breg23] = "DW_OP_breg23",	/* Base register 23.  */
    [DW_OP_breg24] = "DW_OP_breg24",	/* Base register 24.  */
    [DW_OP_breg25] = "DW_OP_breg25",	/* Base register 25.  */
    [DW_OP_breg26] = "DW_OP_breg26",	/* Base register 26.  */
    [DW_OP_breg27] = "DW_OP_breg27",	/* Base register 27.  */
    [DW_OP_breg28] = "DW_OP_breg28",	/* Base register 28.  */
    [DW_OP_breg29] = "DW_OP_breg29",	/* Base register 29.  */
    [DW_OP_breg30] = "DW_OP_breg30",	/* Base register 30.  */
    [DW_OP_breg31] = "DW_OP_breg31",	/* Base register 31.  */
    [DW_OP_regx] = "DW_OP_regx",		/* Unsigned LEB128 register.  */
    [DW_OP_fbreg] = "DW_OP_fbreg",		/* Signed LEB128 offset.  */
    [DW_OP_bregx] = "DW_OP_bregx",		/* ULEB128 register followed by SLEB128 off. */
    [DW_OP_piece] = "DW_OP_piece",		/* ULEB128 size of piece addressed. */
    [DW_OP_deref_size] = "DW_OP_deref_size",	/* 1-byte size of data retrieved.  */
    [DW_OP_xderef_size] = "DW_OP_xderef_size",	/* 1-byte size of data retrieved.  */
    [DW_OP_nop] = "DW_OP_nop",
    [DW_OP_push_object_address] = "DW_OP_push_object_address",
    [DW_OP_call2] = "DW_OP_call2",
    [DW_OP_call4] = "DW_OP_call4",
    [DW_OP_call_ref] = "DW_OP_call_ref",
    [DW_OP_form_tls_address] = "DW_OP_form_tls_address", /* TLS offset to address in current thread */
    [DW_OP_call_frame_cfa] = "DW_OP_call_frame_cfa", /* CFA as determined by CFI.  */
    [DW_OP_bit_piece] = "DW_OP_bit_piece",	/* ULEB128 size and ULEB128 offset in bits.  */
    [DW_OP_implicit_value] = "DW_OP_implicit_value", /* DW_FORM_block follows opcode.  */
    [DW_OP_stack_value] = "DW_OP_stack_value",	 /* No operands, special like [DW_OP_piece.  */

    [DW_OP_implicit_pointer] = "DW_OP_implicit_pointer",
    [DW_OP_addrx] = "DW_OP_addrx",
    [DW_OP_constx] = "DW_OP_constx",
    [DW_OP_entry_value] = "DW_OP_entry_value",
    [DW_OP_const_type] = "DW_OP_const_type",
    [DW_OP_regval_type] = "DW_OP_regval_type",
    [DW_OP_deref_type] = "DW_OP_deref_type",
    [DW_OP_xderef_type] = "DW_OP_xderef_type",
    [DW_OP_convert] = "DW_OP_convert",
    [DW_OP_reinterpret] = "DW_OP_reinterpret",

    /* GNU extensions.  */
    [DW_OP_GNU_push_tls_address] = "DW_OP_GNU_push_tls_address",
    [DW_OP_GNU_uninit] = "DW_OP_GNU_uninit",
    [DW_OP_GNU_encoded_addr] = "DW_OP_GNU_encoded_addr",
    [DW_OP_GNU_implicit_pointer] = "DW_OP_GNU_implicit_pointer",
    [DW_OP_GNU_entry_value] = "DW_OP_GNU_entry_value",
    [DW_OP_GNU_const_type] = "DW_OP_GNU_const_type",
    [DW_OP_GNU_regval_type] = "DW_OP_GNU_regval_type",
    [DW_OP_GNU_deref_type] = "DW_OP_GNU_deref_type",
    [DW_OP_GNU_convert] = "DW_OP_GNU_convert",
    [DW_OP_GNU_reinterpret] = "DW_OP_GNU_reinterpret",
    [DW_OP_GNU_parameter_ref] = "DW_OP_GNU_parameter_ref",

    /* GNU Debug Fission extensions.  */
    [DW_OP_GNU_addr_index] = "DW_OP_GNU_addr_index",
    [DW_OP_GNU_const_index] = "DW_OP_GNU_const_index",

    [DW_OP_GNU_variable_value] = "DW_OP_GNU_variable_value",

    /* The start of the implementation-defined range is used by `DW_OP_GNU_push_tls_address` already. */
    /* [DW_OP_lo_user] = "DW_OP_lo_user", */	/* Implementation-defined range start.  */
    [DW_OP_hi_user] = "DW_OP_hi_user",	/* Implementation-defined range end.  */

    /* Unused values. See `/usr/include/dwarf.h` for the values of the opcodes. */
    /* 169 is the last DWARF opcode. The implementation-defined range starts at 224. */
    [170 ... 223] = "<invalid opcode>",
    /* After `DW_OP_GNU_push_tls_address`, the implementation-defined range is unused
       except for the indices from 240 to 253. */
    [225 ... 239] = "<unused implementation-defined opcode>",
    [254] = "<unused implementation-defined opcode>",
  };

  return operator_names[opcode];
}

uint8_t sd_n_operands(SdOperator opcode) {
  /* The information about the number of operands the
     different opcodes have comes from section 2.5 of
     the DWARF 5 standard. */

  static uint8_t n_operands[256] = {
    [DW_OP_addr] = 1,		/* Constant address.  */
    [DW_OP_deref] = 0,
    [DW_OP_const1u] = 1,	/* Unsigned 1-byte constant.  */
    [DW_OP_const1s] = 1,	/* Signed 1-byte constant.  */
    [DW_OP_const2u] = 1,	/* Unsigned 2-byte constant.  */
    [DW_OP_const2s] = 1,	/* Signed 2-byte constant.  */
    [DW_OP_const4u] = 1,	/* Unsigned 4-byte constant.  */
    [DW_OP_const4s] = 1,	/* Signed 4-byte constant.  */
    [DW_OP_const8u] = 1,	/* Unsigned 8-byte constant.  */
    [DW_OP_const8s] = 1,	/* Signed 8-byte constant.  */
    [DW_OP_constu] = 1,	/* Unsigned LEB128 constant.  */
    [DW_OP_consts] = 1,	/* Signed LEB128 constant.  */
    [DW_OP_dup] = 0,
    [DW_OP_drop] = 0,
    [DW_OP_over] = 0,
    [DW_OP_pick] = 1,		/* 1-byte stack index.  */
    [DW_OP_swap] = 0,
    [DW_OP_rot] = 0,
    [DW_OP_xderef] = 0,
    [DW_OP_abs] = 0,
    [DW_OP_and] = 0,
    [DW_OP_div] = 0,
    [DW_OP_minus] = 0,
    [DW_OP_mod] = 0,
    [DW_OP_mul] = 0,
    [DW_OP_neg] = 0,
    [DW_OP_not] = 0,
    [DW_OP_or] = 0,
    [DW_OP_plus] = 0,
    [DW_OP_plus_uconst] = 1,	/* Unsigned LEB128 addend.  */
    [DW_OP_shl] = 0,
    [DW_OP_shr] = 0,
    [DW_OP_shra] = 0,
    [DW_OP_xor] = 0,
    [DW_OP_bra] = 1,		/* Signed 2-byte constant.  */
    [DW_OP_eq] = 0,
    [DW_OP_ge] = 0,
    [DW_OP_gt] = 0,
    [DW_OP_le] = 0,
    [DW_OP_lt] = 0,
    [DW_OP_ne] = 0,
    [DW_OP_skip] = 1,		/* Signed 2-byte constant.  */
    /* The literals encode the unsigned integer values from 0 through 31, inclusive.
       While not mentioned explicitly, the only option that make sense here is that
       they have 0 operands. */
    [DW_OP_lit0] = 0,		/* Literal 0.  */
    [DW_OP_lit1] = 0,		/* Literal 1.  */
    [DW_OP_lit2] = 0,		/* Literal 2.  */
    [DW_OP_lit3] = 0,		/* Literal 3.  */
    [DW_OP_lit4] = 0,		/* Literal 4.  */
    [DW_OP_lit5] = 0,		/* Literal 5.  */
    [DW_OP_lit6] = 0,		/* Literal 6.  */
    [DW_OP_lit7] = 0,		/* Literal 7.  */
    [DW_OP_lit8] = 0,		/* Literal 8.  */
    [DW_OP_lit9] = 0,		/* Literal 9.  */
    [DW_OP_lit10] = 0,		/* Literal 10.  */
    [DW_OP_lit11] = 0,		/* Literal 11.  */
    [DW_OP_lit12] = 0,		/* Literal 12.  */
    [DW_OP_lit13] = 0,		/* Literal 13.  */
    [DW_OP_lit14] = 0,		/* Literal 14.  */
    [DW_OP_lit15] = 0,		/* Literal 15.  */
    [DW_OP_lit16] = 0,		/* Literal 16.  */
    [DW_OP_lit17] = 0,		/* Literal 17.  */
    [DW_OP_lit18] = 0,		/* Literal 18.  */
    [DW_OP_lit19] = 0,		/* Literal 19.  */
    [DW_OP_lit20] = 0,		/* Literal 20.  */
    [DW_OP_lit21] = 0,		/* Literal 21.  */
    [DW_OP_lit22] = 0,		/* Literal 22.  */
    [DW_OP_lit23] = 0,		/* Literal 23.  */
    [DW_OP_lit24] = 0,		/* Literal 24.  */
    [DW_OP_lit25] = 0,		/* Literal 25.  */
    [DW_OP_lit26] = 0,		/* Literal 26.  */
    [DW_OP_lit27] = 0,		/* Literal 27.  */
    [DW_OP_lit28] = 0,		/* Literal 28.  */
    [DW_OP_lit29] = 0,		/* Literal 29.  */
    [DW_OP_lit30] = 0,		/* Literal 30.  */
    [DW_OP_lit31] = 0,		/* Literal 31.  */
    [DW_OP_reg0] = 0,		/* Register 0.  */
    [DW_OP_reg1] = 0,		/* Register 1.  */
    [DW_OP_reg2] = 0,		/* Register 2.  */
    [DW_OP_reg3] = 0,		/* Register 3.  */
    [DW_OP_reg4] = 0,		/* Register 4.  */
    [DW_OP_reg5] = 0,		/* Register 5.  */
    [DW_OP_reg6] = 0,		/* Register 6.  */
    [DW_OP_reg7] = 0,		/* Register 7.  */
    [DW_OP_reg8] = 0,		/* Register 8.  */
    [DW_OP_reg9] = 0,		/* Register 9.  */
    [DW_OP_reg10] = 0,		/* Register 10.  */
    [DW_OP_reg11] = 0,		/* Register 11.  */
    [DW_OP_reg12] = 0,		/* Register 12.  */
    [DW_OP_reg13] = 0,		/* Register 13.  */
    [DW_OP_reg14] = 0,		/* Register 14.  */
    [DW_OP_reg15] = 0,		/* Register 15.  */
    [DW_OP_reg16] = 0,		/* Register 16.  */
    [DW_OP_reg17] = 0,		/* Register 17.  */
    [DW_OP_reg18] = 0,		/* Register 18.  */
    [DW_OP_reg19] = 0,		/* Register 19.  */
    [DW_OP_reg20] = 0,		/* Register 20.  */
    [DW_OP_reg21] = 0,		/* Register 21.  */
    [DW_OP_reg22] = 0,		/* Register 22.  */
    [DW_OP_reg23] = 0,		/* Register 24.  */
    [DW_OP_reg24] = 0,		/* Register 24.  */
    [DW_OP_reg25] = 0,		/* Register 25.  */
    [DW_OP_reg26] = 0,		/* Register 26.  */
    [DW_OP_reg27] = 0,		/* Register 27.  */
    [DW_OP_reg28] = 0,		/* Register 28.  */
    [DW_OP_reg29] = 0,		/* Register 29.  */
    [DW_OP_reg30] = 0,		/* Register 30.  */
    [DW_OP_reg31] = 0,		/* Register 31.  */
    [DW_OP_breg0] = 1,		/* Base register 0.  */
    [DW_OP_breg1] = 1,		/* Base register 1.  */
    [DW_OP_breg2] = 1,		/* Base register 2.  */
    [DW_OP_breg3] = 1,		/* Base register 3.  */
    [DW_OP_breg4] = 1,		/* Base register 4.  */
    [DW_OP_breg5] = 1,		/* Base register 5.  */
    [DW_OP_breg6] = 1,		/* Base register 6.  */
    [DW_OP_breg7] = 1,		/* Base register 7.  */
    [DW_OP_breg8] = 1,		/* Base register 8.  */
    [DW_OP_breg9] = 1,		/* Base register 9.  */
    [DW_OP_breg10] = 1,	/* Base register 10.  */
    [DW_OP_breg11] = 1,	/* Base register 11.  */
    [DW_OP_breg12] = 1,	/* Base register 12.  */
    [DW_OP_breg13] = 1,	/* Base register 13.  */
    [DW_OP_breg14] = 1,	/* Base register 14.  */
    [DW_OP_breg15] = 1,	/* Base register 15.  */
    [DW_OP_breg16] = 1,	/* Base register 16.  */
    [DW_OP_breg17] = 1,	/* Base register 17.  */
    [DW_OP_breg18] = 1,	/* Base register 18.  */
    [DW_OP_breg19] = 1,	/* Base register 19.  */
    [DW_OP_breg20] = 1,	/* Base register 20.  */
    [DW_OP_breg21] = 1,	/* Base register 21.  */
    [DW_OP_breg22] = 1,	/* Base register 22.  */
    [DW_OP_breg23] = 1,	/* Base register 23.  */
    [DW_OP_breg24] = 1,	/* Base register 24.  */
    [DW_OP_breg25] = 1,	/* Base register 25.  */
    [DW_OP_breg26] = 1,	/* Base register 26.  */
    [DW_OP_breg27] = 1,	/* Base register 27.  */
    [DW_OP_breg28] = 1,	/* Base register 28.  */
    [DW_OP_breg29] = 1,	/* Base register 29.  */
    [DW_OP_breg30] = 1,	/* Base register 30.  */
    [DW_OP_breg31] = 1,	/* Base register 31.  */
    [DW_OP_regx] = 1,		/* Unsigned LEB128 register.  */
    [DW_OP_fbreg] = 1,		/* Signed LEB128 offset.  */
    [DW_OP_bregx] = 2,		/* ULEB128 register followed by SLEB128 off. */
    [DW_OP_piece] = 1,		/* ULEB128 size of piece addressed. */
    [DW_OP_deref_size] = 1,	/* 1-byte size of data retrieved.  */
    [DW_OP_xderef_size] = 1,	/* 1-byte size of data retrieved.  */
    [DW_OP_nop] = 0,
    [DW_OP_push_object_address] = 0,
    [DW_OP_call2] = 1,
    [DW_OP_call4] = 1,
    [DW_OP_call_ref] = 1,
    [DW_OP_form_tls_address] = 0, /* TLS offset to address in current thread */
    [DW_OP_call_frame_cfa] = 0, /* CFA as determined by CFI.  */
    [DW_OP_bit_piece] = 2,	/* ULEB128 size and ULEB128 offset in bits.  */
    [DW_OP_implicit_value] = 2, /* DW_FORM_block follows opcode.  */
    [DW_OP_stack_value] = 0,	 /* No operands, special like [DW_OP_piece.  */

    [DW_OP_implicit_pointer] = 2,
    [DW_OP_addrx] = 1,
    [DW_OP_constx] = 1,
    [DW_OP_entry_value] = 2,
    [DW_OP_const_type] = 3,
    [DW_OP_regval_type] = 2,
    [DW_OP_deref_type] = 2,
    [DW_OP_xderef_type] = 2,
    [DW_OP_convert] = 1,
    [DW_OP_reinterpret] = 1,

    /* Due to lack of documentation, the GNU extensions are not supported for now.
       Instead three is used, such that all possible operands are displayed. */
    [170 ... 223] = 3,			   /* Unused range. */
    [DW_OP_lo_user ... DW_OP_hi_user] = 3,	/* Implementation-defined range.  */
  };

    return n_operands[opcode];
}

dbg_addr dwarf_addr_to_dbg_addr(Dwarf_Addr addr) {
  return (dbg_addr) { addr };
}

#ifndef UNIT_TESTS

typedef struct SdLocRange {
  bool meaningful;  /* Libdwarf returns all types of location descriptions through
		       location list entries. The range specified by this struct are
		       only meaningful for bounded location descriptions. */
  dbg_addr lowpc;		/* Inclusive lower bound. */
  dbg_addr highpc;		/* Exclusive upper bound. */
} SdLocRange;

#endif  // UNIT_TESTS

void sd_init_loc_range(Dwarf_Bool debug_addr_missing,
		       Dwarf_Addr lowpc,
		       Dwarf_Addr highpc,
		       SdLocRange *range_dest) {
  bool invalid_range_values = dwarf_bool_to_bool(debug_addr_missing);
  if (invalid_range_values || (lowpc == 0 && highpc == 0)) {
    /* The range is meaningless in one of the following three cases:
     1. `debug_addr_missing is true, so `lowpc` and `highpc` are invalid.
     2. `lowpc` and `highpc` are both 0 because they were not set by the function call.
     3. `lowpc` and `highpc` were both set to 0 because there is no range for the
        current location description. (It seems that this is how libdwarf generally
	indicates that a location description isn't bounded by any range.) */
    *range_dest = (SdLocRange) {
      .meaningful = false,
      .lowpc = {0},
      .highpc = {0},
    };
  } else {
    *range_dest = (SdLocRange) {
      .meaningful = true,
      .lowpc = dwarf_addr_to_dbg_addr(lowpc),
      .highpc = dwarf_addr_to_dbg_addr(highpc),
    };
  }
}

/* Is the location description associated with this
   location description range active for the given PC?
   Returns `false` for all meaningless range bounds. */
bool is_active_range(SdLocRange range, dbg_addr _pc) {
  if (!range.meaningful) {
    return false;
  } else {
    uint64_t pc = _pc.value;
    uint64_t lowpc = range.lowpc.value;
    uint64_t highpc = range.highpc.value;

    if (lowpc <= pc && pc < highpc) {
      return true;
    } else {
      return false;
    }
  }
}

SprayResult sd_init_loclist(Dwarf_Debug dbg,
			    SdLocAttr loc_attr,
			    SdLoclist *loclist) {
  assert(dbg != NULL);
  assert(loclist != NULL);

  Dwarf_Error error = NULL;
  Dwarf_Attribute attr = loc_attr.attr;

  /* Pointer to the start of the loclist created by `dwarf_get_loclist_c`. */
  Dwarf_Loc_Head_c loclist_head = NULL;
  /* Number of records in the loclist pointed to by `loclist_head`.
     This number is 1 if the loclist  represents a location expression. */
  Dwarf_Unsigned loclist_count = 0;

  int res = dwarf_get_loclist_c(attr, &loclist_head, &loclist_count, &error);

  if (res != DW_DLV_OK) {
    /* Always free memory associated with `loclist_head`. */
    dwarf_dealloc_loc_head_c(loclist_head);
    loclist_head = NULL;
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return SP_ERR;
  } else {
    SdExpression *exprs = calloc(loclist_count, sizeof(SdExpression));
    assert(exprs != NULL);
    SdLocRange *ranges = calloc(loclist_count, sizeof(SdLocRange));
    assert(ranges != NULL);
      
    for (Dwarf_Unsigned i = 0; i < loclist_count; i++) {
      Dwarf_Small _lle_value = 0; /* DW_LLE value if applicable (TODO: find out what this is) */
      /* On success, the first and second operand of the expression respectively.
	 Only applies if the expression has a first or second operand).
	 TODO: This is weird, since the libdwarf docs call these variables `raw[low|hi]pc`.
	 Check if the description above is correct by comparing it to the output of
	 `dwarf_get_location_op_value_c`.*/
      Dwarf_Unsigned _raw_first_op = 0;
      Dwarf_Unsigned _raw_second_op = 0;
      /* Set to true if some required data is missing. Without this data, the cooked values are invalid. */
      Dwarf_Bool debug_addr_missing = false;
      /* The lower (inclusive) and upper (exclusive) bound for a bounded location description
	 in the location list. They represent the range of PC values in which the current
	 location description is active. Location lists contain other entries besides location
	 descriptions. Those include **base addresses**. Those entries should be used as the base
	 address to the lower and upper PC bounds. Base address entries are only needed in CUs where
	 the machine code is split over non-continuous regions (see bullet 'Base address' of section
	 2.6.2 of the DWARF 5 standard). */
      Dwarf_Addr low_pc = 0;
      Dwarf_Addr high_pc = 0;
      /* Number of operations in the expression */
      Dwarf_Unsigned locexpr_op_count = 0;
      /* Pointer to a specific location description. It points to the location description at the current index. */
      Dwarf_Locdesc_c locdesc_entry = NULL;
      /* The applicable DW_LKIND value for the location description (TODO: What is this?) */
      Dwarf_Small _locdesc_lkind = 0;
      /* Offset of the expression in the applicable section (?) */
      Dwarf_Unsigned _expression_offset = 0;
      /* Offset of the location description or zero for simple location expressions. */
      Dwarf_Unsigned _locdesc_offset = 0;
      res = dwarf_get_locdesc_entry_d(loclist_head,
				      i,
				      &_lle_value,
				      &_raw_first_op,
				      &_raw_second_op,
				      &debug_addr_missing,
				      &low_pc,
				      &high_pc,
				      &locexpr_op_count,
				      &locdesc_entry,
				      &_locdesc_lkind,
				      &_expression_offset,
				      &_locdesc_offset,
				      &error);
      if (res != DW_DLV_OK) {
	dwarf_dealloc_loc_head_c(loclist_head);
	if (res == DW_DLV_ERROR) {
	  dwarf_dealloc_error(dbg, error);
	}
	return SP_ERR;
      } else {
	sd_init_loc_range(debug_addr_missing, low_pc, high_pc, &ranges[i]);

	/* Read the location description entry at the current.
	   TODO: It seems that libdwarf returns only DWARF expressions in `locdesc_entry`
	   since all other possible entries seem to be returned in other variables, but
	   I cannot be sure. Check up on this again. */
	res = sd_init_loc_expression(locdesc_entry,
				     locexpr_op_count,
				     &exprs[i],
				     &error);

	if (res != DW_DLV_OK) {
	  if (res == DW_DLV_ERROR) {
	    dwarf_dealloc_error(dbg, error);
	  }

	  dwarf_dealloc_loc_head_c(loclist_head);

	  for (size_t j = 0; j <= i; j++) {
	    del_expression(&exprs[i]);
	  }
	  free(exprs);

	  return SP_ERR;	    
	}
      }
    }
    loclist->exprs = exprs;
    loclist->ranges = ranges;
    loclist->n_exprs = loclist_count;

    return SP_OK;
  }
}

void del_loclist(SdLoclist *loclist) {
  if (loclist != NULL) {
    for (size_t i = 0; i < loclist->n_exprs; i++) {
      del_expression(&loclist->exprs[i]);
    }
    free(loclist->exprs);
    free(loclist->ranges);
    loclist->exprs = NULL;
    loclist->ranges = NULL;
    loclist->n_exprs = 0;
  }
}

/* Calculate the number of digits in the given number. */
unsigned n_digits(double num) {
  if (num == 0) {
    return 1;			/* Zero has one digit when written out. */
  } else {
    return ((unsigned) floor(log10(fabs(num)))) + 1;    
  }
}

/* Print n space characters to standard out. */
void indent_by(unsigned n_spaces) {
  for (unsigned i = 0; i < n_spaces; i++) {
    printf(" ");
  }
}

void print_loclist(SdLoclist loclist) {
  for (size_t i = 0; i < loclist.n_exprs; i++) {
    SdExpression *expr = &loclist.exprs[i];
    SdLocRange *range = &loclist.ranges[i];

    printf("%lu ", i);		/* Print the location list entry index. */
    unsigned n_index_chars = n_digits((double) i) + 1;

    unsigned n_range_chars = 0;
    if (range->meaningful) {
      /* Print the active range for the following entry if it's meaningful. */
      printf("PC: [0x%lx, 0x%lx) ", range->lowpc.value, range->highpc.value);
      n_range_chars = n_digits(range->lowpc.value) + n_digits(range->highpc.value) + 13;
    }

    for (size_t j = 0; j < expr->n_operations; j++) {
      if (j > 0) {
	/* Indent all subsequent expressions to the level of the first one. */
	indent_by(n_index_chars + n_range_chars);	
      }

      SdOperation *op = &expr->operations[j];
      printf("%s", sd_what_operator(op->opcode));

      /* Print all operands required by the operator. */
      for (uint8_t k = 0; k < sd_n_operands(op->opcode); k++) {
	if (k == 0) {
	  printf(":");
	}

	printf(" %lld", op->operands[k]);
      }

      printf("\n");
    }
  }
}


SdLocation sd_loc_addr(real_addr addr) {
  return (SdLocation) {
    .tag = LOC_ADDR,
    .addr = addr,
  };
}

SdLocation sd_loc_as_addr(uint64_t addr) {
  return (SdLocation) {
    .tag = LOC_ADDR,
    .addr = (real_addr){addr},
  };
}

SdLocation sd_loc_reg(x86_reg reg) {
  return (SdLocation) {
    .tag = LOC_REG,
    .reg = reg,
  };
}

/* Evaluation stack. */

typedef SdLocation SdStackElem;

typedef struct LocEvalStack {
  SdStackElem *buf;		/* The stack itself */
  size_t sp;			/* Pointer to the next unused element on the stack.
				   (points one element past the top)*/
  size_t n_alloc;		/* Number of elements allocated but not necessarily used. */
} LocEvalStack;

enum { LOC_EVAL_STACK_BLOCK = 32 };

LocEvalStack init_eval_stack(void) {
  LocEvalStack stack = {
    .sp = 0,
    .n_alloc = LOC_EVAL_STACK_BLOCK,
    .buf = NULL,
  };

  stack.buf = calloc(stack.n_alloc, sizeof(*stack.buf));
  assert(stack.buf != NULL);

  return stack;
}

/* Pop the top-most element off the stack.

   If the stack is not empty, the value of the top
   element is stored in `pop_into` and `SP_OK` is returned.

   `SP_ERR` is returned if the stack is empty and `pop_into`
   stays untouched. */
SprayResult pop_eval_stack(LocEvalStack *stack, SdStackElem *pop_into) {
  assert(stack != NULL);
  assert(pop_into != NULL);

  if (stack->sp > 0) {
    stack->sp -= 1;
    *pop_into = stack->buf[stack->sp];
    return SP_OK;
  } else {
    /* Stack is empty. */
    return SP_ERR;
  }
}

/* Push the given element on top of the stack. */
void push_eval_stack(LocEvalStack *stack, SdStackElem push) {
  assert(stack != NULL);

  stack->buf[stack->sp] = push;
  stack->sp += 1;

  if (stack->sp >= stack->n_alloc) {
    stack->n_alloc += LOC_EVAL_STACK_BLOCK;
    stack->buf = realloc(stack->buf, sizeof(*stack->buf) * stack->n_alloc);
    assert(stack->buf != NULL);
  }
}

void del_eval_stack(LocEvalStack *stack) {
  if (stack != NULL) {
    free(stack->buf);
    stack->buf = NULL;
  }
}

SprayResult sd_eval_op_fbreg(Dwarf_Debug dbg,
			     SdLocEvalCtx ctx,
			     SdOperation self,
			     LocEvalStack *stack) {
  /* DW_OP_fbreg. See DWARF 5 standard, section 2.5.1.2, bullet 1. */

  const Elf64_Sym *subprog = se_symbol_from_addr(ctx.pc, ctx.elf);
  if (subprog == NULL) {
    return SP_ERR;
  }

  const char *subprog_name = se_symbol_name(subprog, ctx.elf);
  if (subprog_name == NULL) {
    return SP_ERR;
  }

  SdLocAttr loc_attr = {0};
  SprayResult res = sd_get_subprog_loc_attr(dbg, subprog_name, DW_AT_frame_base, &loc_attr);
  if (res == SP_ERR) {
    return SP_ERR;
  }

  SdLoclist loclist = {0};
  res = sd_init_loclist(dbg, loc_attr, &loclist);
  if (res == SP_ERR) {
    return SP_ERR;
  }

  SdLocation location = {0};
  res = sd_eval_loclist(dbg, ctx, loclist, &location);
  del_loclist(&loclist);
  
  if (res == SP_ERR) {
    return SP_ERR;
  }

  if (location.tag == LOC_REG) {
    uint64_t base_addr = 0;
    res = get_register_value(ctx.pid, location.reg, &base_addr);
    if (res == SP_ERR) {
      return SP_ERR;
    } else {
      /* `op.operand1` is a signed value. */
      uint64_t loc = (uint64_t)((int64_t)base_addr + (int64_t)self.operand1);
      SdStackElem push = sd_loc_as_addr(loc);
      push_eval_stack(stack, push);
      return SP_OK;
    }
  } else {
    return SP_ERR;
  }
}

SprayResult sd_eval_op_regn(Dwarf_Debug dbg,
			    SdLocEvalCtx ctx,
			    SdOperation self,
			    LocEvalStack *stack) {
  /* DW_OP_reg0 - DW_OP_reg31. See DWARF 5 standard, section 2.6.1.1.3. */
  assert(stack != NULL);
  unused(dbg);
  unused(ctx);

  uint8_t dwarf_regnum = self.opcode - DW_OP_reg0;
  x86_reg reg = 0;
  bool could_translate = dwarf_regnum_to_x86_reg(dwarf_regnum, &reg);
  if (could_translate) {
    push_eval_stack(stack, sd_loc_reg(reg));
    return SP_OK;
  } else {
    /* The DWARF register number doesn't represent a
       (supported) register on x86. */
    return SP_ERR;
  }
}

SprayResult sd_eval_op_addr(Dwarf_Debug dbg,
			    SdLocEvalCtx ctx,
			    SdOperation self,
			    LocEvalStack *stack) {
  assert(stack != NULL);
  unused(dbg);

  dbg_addr operand_addr = {self.operand1};
  real_addr addr = dbg_to_real(ctx.load_address, operand_addr);
  push_eval_stack(stack, sd_loc_addr(addr));

  return SP_OK;
}

/* Function pointer to functions that evaluate operations. */
typedef SprayResult (*SdEvalOp)(Dwarf_Debug dbg,
				SdLocEvalCtx ctx,
				SdOperation self,
				LocEvalStack *stack);

SdEvalOp sd_get_eval_handler(SdOperator opcode) {
  static const SdEvalOp op_handlers[256] = {
    [DW_OP_reg0 ... DW_OP_reg31] = sd_eval_op_regn,
    [DW_OP_fbreg] = sd_eval_op_fbreg,
    [DW_OP_addr] = sd_eval_op_addr,

    /* Rest is `NULL`. */
  };

  return op_handlers[opcode];
}

SprayResult sd_eval_locop(Dwarf_Debug dbg,
			  SdLocEvalCtx ctx,
			  SdOperation op,
			  LocEvalStack *stack) {
  assert(dbg != NULL);
  assert(stack != NULL);

  SdEvalOp eval_handler = sd_get_eval_handler(op.opcode);
  if (eval_handler == NULL) {
    /* Operation `opcode` is not supported. */
    return SP_ERR;
  } else {
    /* Evaluate the operation with the given handler. */
    return eval_handler(dbg, ctx, op, stack);
  }
}

SprayResult sd_eval_locexpr(Dwarf_Debug dbg,
			    SdLocEvalCtx ctx,
			    SdExpression locexpr,
			    SdLocation *location) {
  LocEvalStack stack = init_eval_stack();

  for (size_t i = 0; i < locexpr.n_operations; i++) {
    SprayResult res = sd_eval_locop(dbg, ctx, locexpr.operations[i], &stack);
    if (res == SP_ERR) {
      /* Abort the entire evaluation. */
      del_eval_stack(&stack);
      return SP_ERR;
    }
  }

  /* Return the top element of the stack. */
  SdStackElem top;
  pop_eval_stack(&stack, &top);
  *location = top;

  del_eval_stack(&stack);

  return SP_OK;
}

SprayResult sd_eval_loclist(Dwarf_Debug dbg,
			    SdLocEvalCtx ctx,
			    SdLoclist loclist,
			    SdLocation *location) {
  /* First, try to evaluate the first active bounded range that's found.
     The DWARF 5 standard says that if two range bounds overlap, then the
     object can be found at both locations at the same time. Hence, it doesn't
     matter which active range is found first. */
  for (size_t i = 0; i < loclist.n_exprs; i++) {
    SdLocRange range = loclist.ranges[i];
    if (is_active_range(range, ctx.pc)) {
      return sd_eval_locexpr(dbg, ctx, loclist.exprs[i], location);
    }
  }

  /* If none of the bounded location descriptions in this location
     list are active, evaluate the first location description that
     doesn't have a range bound and is always active. */
  for (size_t i = 0; i < loclist.n_exprs; i++) {
    SdLocRange range = loclist.ranges[i];
    if (!range.meaningful) {
      return sd_eval_locexpr(dbg, ctx, loclist.exprs[i], location);
    }    
  }

  /* There is no location description in this location
     list that's active at the moment. */
  *location = sd_loc_as_addr(0x0);
  return SP_OK;
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
       from the source directory, not the proper relative file path.
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

bool callback__get_filepaths(Dwarf_Debug dbg,
			     Dwarf_Die cu_die,
			     SearchFor search_for,
			     SearchFindings search_findings) {  
  unused(search_for);

  if (sd_has_tag(dbg, cu_die, DW_TAG_compile_unit)) {
    Filepaths *filepaths = (Filepaths *) search_findings.data;

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

bool callback__get_srclines(Dwarf_Debug dbg,
			    Dwarf_Die cu_die,
			    SearchFor search_for,
			    SearchFindings search_findings) {
  const char *filepath = (const char *) search_for.data;
  LineTable *line_table = (LineTable *) search_findings.data;
  
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
                                      dbg_addr pc,
                                      unsigned *index_dest) {
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

LineEntry sd_line_entry_from_pc(Dwarf_Debug dbg, dbg_addr pc) {
  assert(dbg != NULL);

  char *filepath = sd_filepath_from_pc(dbg, pc);
  if (filepath == NULL) {
    return (LineEntry) { .is_ok=false };
  }

  LineTable line_table = sd_get_line_table(dbg, filepath);
  free(filepath);
  if (!line_table.is_set) {
    return (LineEntry) { .is_ok=false };
  }

  for (unsigned i = 0; i < line_table.n_lines; i++) {
    if (line_table.lines[i].addr.value == pc.value) {
      LineEntry ret = line_table.lines[i];
      ret.is_exact = true;
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
    LineEntry ret = line_table.lines[pc_line_idx];
    sd_free_line_table(&line_table);
    // Does the line entry match the given PC exactly?
    if (ret.is_ok && ret.addr.value == pc.value) {
      ret.is_exact = true;
    }
    return ret;
  }
}

LineEntry sd_line_entry_at(Dwarf_Debug dbg, const char *filepath,
                           unsigned lineno) {
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

typedef struct {
  /* Addresses are unsigned and we should allow them
     to have any value. Therefore `is_set` signals
     whether or not they are set. The alternative of
     using e.g. `-1` as the unset value doesn't work. */
  bool is_set;
  dbg_addr lowpc;
  dbg_addr highpc;
} SubprogPcRange;

/* Search callback that looks for a DIE describing the
   subprogram with the name `search_for` and stores the
   attributes `AT_low_pc` and `AT_high_pc` in `search_findings`. */
bool callback__find_subprog_pc_range_by_subprog_name(Dwarf_Debug dbg,
						     Dwarf_Die die,
						     SearchFor search_for,
						     SearchFindings search_findings) {
  const char *fn_name = (const char *) search_for.data;
  SubprogPcRange *range = (SubprogPcRange *) search_findings.data;

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

    range->lowpc = (dbg_addr) { lowpc };
    range->highpc = (dbg_addr) { highpc };
    range->is_set = true;
    return true;
  } else {
    return false;
  }
}

SubprogPcRange sd_get_subprog_pc_range(Dwarf_Debug dbg, const char *fn_name) {
  assert(dbg != NULL);
  assert(fn_name != NULL);

  Dwarf_Error error = NULL;
  SubprogPcRange range = { .is_set=false };

  int res = sd_search_dwarf_dbg(dbg, &error,  
    callback__find_subprog_pc_range_by_subprog_name,
    fn_name, &range);

  if (res != DW_DLV_OK) {
    if (res == DW_DLV_ERROR) {
      dwarf_dealloc_error(dbg, error);
    }
    return (SubprogPcRange) { .is_set=false };
  } else {
    return range;
  }
}

SprayResult sd_for_each_line_in_subprog(Dwarf_Debug dbg, const char *fn_name,
                                        const char *filepath,
                                        LineCallback callback,
                                        void *const init_data) {
  assert(dbg != NULL);
  assert(fn_name != NULL);
  assert(filepath != NULL);
  assert(callback != NULL);
  assert(init_data != NULL);

  SubprogPcRange attr = sd_get_subprog_pc_range(dbg, fn_name);
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

SprayResult sd_effective_start_addr(Dwarf_Debug dbg,
				    dbg_addr prologue_start,
                                    dbg_addr function_end,
                                    dbg_addr *function_start) {
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
