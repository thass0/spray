/* Cache of source files that were read to
   print the source context around breakpoints. */

#pragma once

#ifndef _SPRAY_SOURCE_FILES_H_
#define _SPRAY_SOURCE_FILES_H_

#include <stdio.h>

#include "magic.h"
#include "hashmap.h"

typedef struct {
  char **lines;
  size_t n_lines;  /* Number of lines stores in `lines`. */
  size_t n_alloc;  /* Number of lines allocated in `lines`. */
  char *filepath;  /* This is the hashmap's key. */
} SourceLines;

typedef struct hashmap SourceFiles;

SourceFiles *init_source_files(void);

void free_source_files(SourceFiles *source_files);

SprayResult print_source(
  SourceFiles *source_files,
  const char *source_filepath,
  unsigned lineno,
  unsigned n_context
);

#endif  // _SPRAY_SOURCE_FILES_H_
