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
    for (size_t i = 0; i < lines->n_lines; i++) {
      free(lines->lines[i]);
    }
    free(lines->lines);
    free(lines->filepath);
  }
  hashmap_free(source_files);
}

enum print_source_block_size {
  LINE_BLOCK_SIZE = 128,
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

    size_t n_alloc = LINE_BLOCK_SIZE;
    SourceLines lines = {
        .lines = calloc(n_alloc, sizeof(char *)),
        .n_lines = 0,
        .n_alloc = n_alloc,
        .filepath = calloc(strlen(source_filepath) + 1, sizeof(char)),
    };
    assert(lines.lines != NULL);
    assert(lines.filepath != NULL);

    strcpy(lines.filepath, source_filepath);

    size_t n_chars_read = 0;  /* Required by `getline(3)` not used rn. */
    for (; lines.n_lines < lines.n_alloc; lines.n_lines++) {
      // Increase the array size to prevent the loop from ending.
      if (lines.n_lines + 1 >= lines.n_alloc) {
        size_t new_alloc_start_offset = lines.n_alloc;
        lines.n_alloc += LINE_BLOCK_SIZE;
        lines.lines =
            realloc(lines.lines, sizeof(*lines.lines) * lines.n_alloc);
        // Zero the new memory.
        memset(lines.lines + new_alloc_start_offset,
          0, LINE_BLOCK_SIZE * sizeof(char *));
      }

      if (getline(&lines.lines[lines.n_lines], &n_chars_read, f) == -1) {
        // We hit the EOF, increase `lines.n_lines` once
        // more to account for the previous line.
        lines.n_lines ++;
        break;
      }
    }

    fclose(f);

    hashmap_set(source_files, &lines);
    return hashmap_get(source_files, &lines);
  }
}

// Definied in `colorize.scm`. Prints out a colored version of the source code.
extern void print_colored(const char *code);

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

  /* Is the line context we want to display
     outside of the possible range of lines? */
  if (end_lineno > lines->n_lines) {
    end_lineno = lines->n_lines;
  }

  /* NOTE: Line numbers are one-indexed; we must
     subtract 1 to use them as array indices. */
  /*const char *cur_line = NULL;
  for (
    unsigned cur_lineno = start_lineno;
    cur_lineno < end_lineno;
    cur_lineno++
  ) {
    printf(" %4d", cur_lineno);
    cur_line = lines->lines[cur_lineno - 1];
    if (cur_lineno == lineno) {
      // Highlight current line.
      fputs(" -> ", stdout);
    } else if (strlen(cur_line) > 1) {
      fputs("    ", stdout);
    }

    // The string read by `getline(3)` ends in a newline
    // so we don't need to add one ourselves.
    fputs(cur_line, stdout);
  }*/

  char *code = NULL;
  for (unsigned lineno = start_lineno;
       lineno < end_lineno;
       lineno++) {
    size_t len = code == NULL ? 0 : strlen(code);
    size_t added = strlen(lines->lines[lineno - 1]);
    code = realloc(code, len + added + 1);
    memcpy(code + len, lines->lines[lineno - 1], added);
    code[len + added] = '\0';
  }
  print_colored(code);
  free(code);

  return SP_OK;
}

