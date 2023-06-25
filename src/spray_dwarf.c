#include "spray_dwarf.h"

#include <dwarf.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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


int search_dwarf_die(Dwarf_Debug dbg, Dwarf_Die in_die, Dwarf_Error *const error,
  int is_info, int in_level,
  bool (*search_callback)(Dwarf_Debug, Dwarf_Die, void *const, void *const),
  void *const search_for, void *const search_findings
) {  
  int res = DW_DLV_OK;
  Dwarf_Die cur_die = in_die;
  Dwarf_Die child_die = NULL;

  search_callback(dbg, in_die, search_for, search_findings);

  while (1) {
    Dwarf_Die sib_die = NULL;

    res = dwarf_child(cur_die, &child_die, error);
    if (res == DW_DLV_ERROR) {
      return DW_DLV_ERROR;
    } else if (res == DW_DLV_OK) {
      /* We found a child: recurse! */
      search_dwarf_die(dbg, child_die, error,
        is_info, in_level + 1,
        search_callback, search_for, search_findings);
  
      dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
      child_die = NULL;
    }

    /* `DW_DLV_OK` or `DW_DLV_NO_ENTRY`. */

    res = dwarf_siblingof_b(dbg, cur_die,
      is_info, &sib_die,
      error);
    if (res == DW_DLV_ERROR) {
      exit(1);
    } else {
      /* Is `cur_die` a sibling of the initial DIE? */
      if (cur_die != in_die) {
        dwarf_dealloc(dbg, cur_die, DW_DLA_DIE);
        cur_die = NULL;
      }
    }

    if (res == DW_DLV_NO_ENTRY) {
      /* Level is empty now. */
      return DW_DLV_OK;
    } else if (res == DW_DLV_OK) {
      cur_die = sib_die;
      search_callback(dbg, sib_die, search_for, search_findings);
    }
  }
}

/* Search the `Dwarf_Debug` instance for `search_for`. For each DIE
   `search_callback` is called and passed `search_for` and `search_findings`
   in that order. It checks if `search_for` is found in the DIE and stores
   the findins in `search_findings`.
   `search_callback` returns `true` if `search_for` has been found. */
int search_dwarf_dbg(Dwarf_Debug dbg, Dwarf_Error *const error,
  bool (*search_callback)(Dwarf_Debug, Dwarf_Die, void *const, void *const),
  void *const search_for, void *const search_findings
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

    res = search_dwarf_die(dbg, cu_die, error, is_info, 0,
      search_callback, search_for, search_findings);
  }
}

/* Switches `pc_in_die` to `true` if it finds that `pc` lies
   between `DW_AT_pc_low` and `DW_AT_pc_hight` of the DIE. */
int check_pc_in_die(Dwarf_Die die, Dwarf_Error *const error, Dwarf_Addr pc, bool *pc_in_die) {
  int res = DW_DLV_OK;
  Dwarf_Half die_tag = 0;
  res = dwarf_tag(die,
    &die_tag, error);

  if (res != DW_DLV_OK) {
    return res;
  }

  if (die_tag == DW_TAG_subprogram) {
    Dwarf_Addr low_addr = 0;
    Dwarf_Addr high_addr = 0;
    res = dwarf_lowpc(die, &low_addr, error);
    if (res != DW_DLV_OK) {
      return res;
    }
    Dwarf_Half high_form = 0;
    enum Dwarf_Form_Class high_class = DW_FORM_CLASS_UNKNOWN;
    res = dwarf_highpc_b(die,
      &high_addr, &high_form,
      &high_class, error);
    if (res != DW_DLV_OK) {
      return res;
    }

    if (
      high_form != DW_FORM_addr &&
      !dwarf_addr_form_is_indexed(high_form)
    ) {
      /* `high_addr` was just an offset of `low_addr`. */
      high_addr += low_addr;
    }

    if (low_addr <= pc && pc <= high_addr) {
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
bool find_name_in_die(Dwarf_Debug dbg, Dwarf_Die die,
  void *const search_for, void *const search_findings
) {
  Dwarf_Addr *pc = (Dwarf_Addr *) search_for;
  char **fn_name = (char **) search_findings;

  int res = DW_DLV_OK;
  Dwarf_Error error = NULL;
  bool found_pc = false;

  res = check_pc_in_die(die, &error, *pc, &found_pc);

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
  char *fn_name = NULL;  // <- Store the function name here.
  Dwarf_Addr pc_addr = pc.value;
  
  int res = search_dwarf_dbg(dbg, &error,
    find_name_in_die,
    &pc_addr,
    &fn_name);

  if (res == DW_DLV_ERROR) {
    dwarf_dealloc_error(dbg, error);
    return NULL;
  } else if (res == DW_DLV_NO_ENTRY) {
    return NULL;
  } else {
    /* Success. `find_name_in_die` must have
       allocated memory for `fn_name`. */
    assert(fn_name != NULL);
    return fn_name;
  }
}

