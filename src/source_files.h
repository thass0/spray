/* Cache of source files that were read to
   print the source context around breakpoints. */

#pragma once

#ifndef _SPRAY_SOURCE_FILES_H_
#define _SPRAY_SOURCE_FILES_H_

#include <stdio.h>

#include "magic.h"
#include "hashmap.h"

typedef struct {
  char *code;			/* Content of source file. */
  size_t *line_lengths;        /* Lengths of each line in `code`, including the trailing '\n'. */
  size_t n_lines;       	/* Number of  elements in `line_lengths`. */
  char *filepath;		/* Path to source file. This is the hashmap's key. */
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
