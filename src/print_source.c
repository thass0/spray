#include "print_source.h"
#include "args.h"

#include <stdbool.h>
#include <chicken.h>

void init_print_source(void) {
  CHICKEN_run(C_toplevel);
}

// Definied in `src/source-files.scm`.
extern int print_source_extern(const char *filepath,
			       unsigned lineno,
			       unsigned n_context_lines,
			       bool use_color);

SprayResult print_source(const char *filepath,
			 unsigned lineno,
			 unsigned n_context) {
  bool use_color = !get_args()->flags.no_color;
  int res = print_source_extern(filepath,
				lineno,
				n_context,
				use_color);
  if (res == 0) {
    return SP_OK;
  } else {
    return SP_ERR;
  }
}
