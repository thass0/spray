#pragma once

#ifndef _SPRAY_PRINT_SOURCE_H_
#define _SPRAY_PRINT_SOURCE_H_

#include "magic.h"

// Call this to initialize `print_source`. The program
// will crash if `print_source` is called without being
// initialized.
void init_print_source(void);

SprayResult print_source(
  const char *source_filepath,
  unsigned lineno,
  unsigned n_context
);

#endif  // _SPRAY_PRINT_SOURCE_H_
