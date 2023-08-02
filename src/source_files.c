#include "source_files.h"

#include "magic.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <chicken.h>  // For `CHICKEN_run` to make `print_colored` available.

int source_lines_compare(const void *a, const void *b, void *user_data) {
  unused(user_data);
  const SourceLines *lines_a = (SourceLines *) a;
  const SourceLines *lines_b = (SourceLines *) b;
  return strcmp(lines_a->filepath, lines_b->filepath);
}

uint64_t source_lines_hash(const void *entry, uint64_t seed0, uint64_t seed1) {
  const SourceLines *lines = (SourceLines *) entry;
  return hashmap_murmur(lines->filepath, strlen(lines->filepath), seed0, seed1);
}

SourceFiles *init_source_files(void) {
  CHICKEN_run(C_toplevel);  // Make `print_colored` available.
  return hashmap_new(sizeof(SourceLines), 0, 0, 0,
    source_lines_hash, source_lines_compare, NULL, NULL);
}

void free_source_files(SourceFiles *source_files) {
  assert(source_files != NULL);
  size_t iter = 0;
  void *entry;
  while (hashmap_iter(source_files, &iter, &entry)) {
    const SourceLines *lines = entry;
    free(lines->line_lengths);
    free(lines->code);
    free(lines->filepath);
  }
  hashmap_free(source_files);
}

enum {
  LINE_LENGTH_BLOCK = 64,
  CODE_BLOCK = 1024,
};

const SourceLines *get_source_lines(SourceFiles *source_files, const char *source_filepath) {
  assert(source_files != NULL);
  assert(source_filepath != NULL);

  SourceLines lookup = { .filepath = (char *) source_filepath };
  const SourceLines *lines = hashmap_get(source_files, &lookup);

  if (lines != NULL) {
    return lines;
  } else {
    FILE *f = fopen(source_filepath, "r");
    if (f == NULL) {
      return NULL;
    }

    size_t code_size = 0;
    size_t code_size_alloc = CODE_BLOCK;
    size_t code_offset = 0;

    size_t n_lines_alloc = LINE_LENGTH_BLOCK;

    SourceLines lines = {
      .code = calloc(code_size_alloc, sizeof(char)),
      .line_lengths = calloc(n_lines_alloc, sizeof(size_t)),
      .n_lines = 0,
      .filepath = calloc(strlen(source_filepath) + 1, sizeof(char)),
    };
    assert(lines.code != NULL);
    assert(lines.line_lengths != NULL);
    assert(lines.filepath != NULL);
    strcpy(lines.filepath, source_filepath);

    size_t line_length = 0;
    size_t getline_buf_size = 0;
    char *getline_buf = NULL;
    while (getline(&getline_buf, &getline_buf_size, f) != -1) {
      line_length = strlen(getline_buf);

      if (lines.n_lines >= n_lines_alloc) {
	n_lines_alloc += LINE_LENGTH_BLOCK;
	lines.line_lengths = realloc(lines.line_lengths, sizeof(size_t) * n_lines_alloc);
	assert(lines.line_lengths != NULL);
      }
      lines.line_lengths[lines.n_lines++] = line_length;

      code_offset = code_size;
      code_size += line_length;
      if (code_size >= code_size_alloc) {
	code_size_alloc += CODE_BLOCK;
	lines.code = realloc(lines.code, sizeof(char) * code_size_alloc);
	assert(lines.code != NULL);
      }

      strncpy(lines.code + code_offset, getline_buf, line_length);
    }

    free(getline_buf);

    /* Add a NULL-terminator. */
    code_size += 1;
    if (code_size >= code_size_alloc) {
      lines.code = realloc(lines.code, sizeof(char) * code_size);
      assert(lines.code != NULL);
    }
    lines.code[code_size - 1] = '\0';

    fclose(f);

    hashmap_set(source_files, &lines);
    return hashmap_get(source_files, &lines);
  }
}

/* Return the offset at which the line at `lineno` starts. */
SprayResult line_offset(const size_t *line_lengths, size_t n_lengths, unsigned lineno, size_t *offset) {
  assert(line_lengths != NULL);
  assert(offset != NULL);

  size_t n_acc_lengths = lineno - 1;
  if (n_acc_lengths > n_lengths) {
    n_acc_lengths = n_lengths;
  }

  /* Accumulate all offsets until we reach the given line number. */
  size_t offset_acc = 0;
  for (size_t i = 0; i < n_acc_lengths; i++) {
    offset_acc += line_lengths[i];
  }
  *offset = offset_acc;

  return SP_OK;
}

// Defined in `colorize.scm`. Prints out a colored version of the source code.
extern void print_colored(const char *code, unsigned start_lineno,
			  unsigned active_lineno, bool use_color);

SprayResult print_source(
  SourceFiles *source_files,
  const char *source_filepath,
  unsigned lineno,
  unsigned n_context
) {
  assert(source_files != NULL);
  assert(source_filepath != NULL);

  const SourceLines *lines = get_source_lines(source_files, source_filepath);
  if (lines == NULL) {
    return SP_ERR;
  }

  /* Calculate context window into file. */
  unsigned start_lineno = lineno <= n_context ? 1 : lineno - n_context;
  unsigned end_lineno = lineno + n_context + 1;

  /* Does the desired context exceed the upper limit? */
  if (lineno < n_context) {
    end_lineno += n_context - lineno;
  }

  size_t start_offset = 0;
  line_offset(lines->line_lengths, lines->n_lines, start_lineno, &start_offset);
  size_t end_offset = 0;
  line_offset(lines->line_lengths, lines->n_lines, end_lineno, &end_offset);

  char temp = lines->code[end_offset];
  lines->code[end_offset] = '\0';

  print_colored(lines->code + start_offset, start_lineno, lineno, 0);

  lines->code[end_offset] = temp;

  return SP_OK;
}

