#include "dwarf.h"

#include <libelfin/dwarf/dwarf++.hh>
#include <libelfin/elf/elf++.hh>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstring>

w_elf_elf elf_elf_ctor_mmap(int fd) {
  try {
    // Get the `elf` instance and copy it.
    elf::elf elf = elf::elf {elf::create_mmap_loader(fd)};
    elf::elf *w_elf =
      static_cast<elf::elf*>(malloc (sizeof(elf::elf)));
    memcpy(w_elf, &elf, sizeof(elf::elf));
    return static_cast<w_elf_elf>(w_elf);
  } catch (...) {
    return NULL;
  }
}

void elf_elf_dtor(w_elf_elf w_elf) {
  elf::elf *elf = static_cast<elf::elf*>(w_elf);
  free(elf);
}


w_dwarf_dwarf dwarf_dwarf_ctor_elf_loader(w_elf_elf w_elf) {
  assert(w_elf != NULL);
  try {
    /* Construct an instance of `dwarf::dwarf`. */
    elf::elf *elf = static_cast<elf::elf*>(w_elf);
    dwarf::dwarf dwarf = dwarf::dwarf {dwarf::elf::create_loader(*elf)};
    dwarf::dwarf *w_dwarf =
      static_cast<dwarf::dwarf*>(malloc (sizeof(dwarf::dwarf)));
    memcpy(w_dwarf, &dwarf, sizeof(dwarf::dwarf));
    return static_cast<w_dwarf_dwarf>(w_dwarf);
  } catch (...) {
    return NULL;
  }
}

void dwarf_dwarf_dtor(w_dwarf_dwarf w_dwarf) {
  dwarf::dwarf *dwarf = static_cast<dwarf::dwarf*>(w_dwarf);
  free(dwarf);
}

w_dwarf_die get_function_from_pc(w_dwarf_dwarf w_dwarf, x86_addr pc) {
  assert(w_dwarf != NULL);

  dwarf::dwarf *dwarf = static_cast<dwarf::dwarf*>(w_dwarf);
  dwarf::taddr pc_addr = pc.value;

  /* Which compilation unit contains the given PC. */
  for (const dwarf::compilation_unit& cu: dwarf->compilation_units()) {
    if (dwarf::die_pc_range(cu.root()).contains(pc_addr)) {

      /* Which subprogram (i.e. function) DIE in that CU contains the given PC? */
      for (const dwarf::die& die : cu.root()) {
        if (die.tag == dwarf::DW_TAG::subprogram) {
          if (die_pc_range(die).contains(pc_addr)) {
            /* Copy this DIE and return it. */
            dwarf::die *w_die = static_cast<dwarf::die*>(malloc (sizeof(dwarf::die)));
            memcpy(w_die, &die, sizeof(dwarf::die));
            return static_cast<w_dwarf_die>(w_die);
          }
        }
      }

    }
  }

  /* No function contains the PC. */
  return NULL;
}

void dwarf_die_dtor(w_dwarf_die w_die) {
  dwarf::die *die = static_cast<dwarf::die*>(w_die);
  free(die);
}

const char *get_function_name_from_die(w_dwarf_die w_die) {
  dwarf::die *die = static_cast<dwarf::die*>(w_die);

  if (die->has(dwarf::DW_AT::name)) {
    const char *raw_section_data_str = nullptr;
    size_t n_chars = 0;
    try {
      dwarf::value at_name = (*die)[dwarf::DW_AT::name];
      /* `value::as_cstr` returns a pointer into the section data.
         Therefore we must take ownership of the string by copying it. */
      raw_section_data_str = at_name.as_cstr(&n_chars);
    } catch (...) {
      /* `dwarf::die` throws if `dwarf::DW_AT::name` is out of range
         (we check that though) and `value::as_cstr` throws if it
         can't convert the value to string (shouldn't happen either). */
      return NULL;
    }
    char *function_name =
      static_cast<char*>(calloc(n_chars + 1, sizeof(char)));
    strncpy(function_name, raw_section_data_str, n_chars);
    return function_name;
  } else {
    /* The DIE doesn't have the attribute we are looking for. */
    return NULL;
  }
}
