#include "source_files.h"

#include "magic.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

int source_lines_compare(const void *a, const void *b, void *udata) {
  unused(udata);
  const SourceLines *lines_a = (SourceLines *) a;
  const SourceLines *lines_b = (SourceLines *) b;
  return strcmp(lines_a->filepath, lines_b->filepath);
}

uint64_t source_lines_hash(const void *entry, uint64_t seed0, uint64_t seed1) {
  const SourceLines *lines = (SourceLines *) entry;
  return hashmap_murmur(lines->filepath, strlen(lines->filepath), seed0, seed1);
}

SourceFiles *init_source_files(void) {
  return hashmap_new(sizeof(SourceLines), 0, 0, 0,
    source_lines_hash, source_lines_compare, NULL, NULL);
}

void free_source_files(SourceFiles *source_files) {
  assert(source_files != NULL);
  size_t iter = 0;
  void *entry;
  while (hashmap_iter(source_files, &iter, &entry)) {
    const SourceLines *lines = (SourceLines *) entry;
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
  const SourceLines *lines = (SourceLines *) hashmap_get(source_files, &lookup);

  if (lines != NULL) {
    return lines;
  } else {
    FILE *f = fopen(source_filepath, "r");
    if (f == NULL) {
      return NULL;
    }

    size_t n_alloc = LINE_BLOCK_SIZE;
    SourceLines lines = {
      .lines = (char **) calloc (n_alloc, sizeof(char *)),
      .n_lines = 0,
      .n_alloc = n_alloc,
      .filepath = (char *) calloc (strlen(source_filepath) + 1, sizeof(char)),
    };
    assert(lines.lines != NULL);
    /* Copy the filepath. */
    assert(lines.filepath != NULL);
    strcpy(lines.filepath, source_filepath);

    size_t n_chars_read = 0;  /* Required by `getline(3)` not used rn. */
    for (; lines.n_lines < lines.n_alloc; lines.n_lines++) {
      /* Allocate more memory before the first loop
         condition fails. */
      if (lines.n_lines + 1 >= lines.n_alloc) {
        size_t new_alloc_start_offset = lines.n_alloc;
        lines.n_alloc += LINE_BLOCK_SIZE;
        lines.lines =
          (char **) realloc (lines.lines, lines.n_alloc* sizeof(char *));
        /* Zero all the newly allocated memory. */
        memset(lines.lines + new_alloc_start_offset,
          0, LINE_BLOCK_SIZE * sizeof(char *));
      }

      if (getline(&lines.lines[lines.n_lines], &n_chars_read, f) == -1) {
        /* EOF. Must increase manually `n_lines`
           because the loop won't restart. */
        lines.n_lines ++;
        break;
      }
    }

    fclose(f);

    hashmap_set(source_files, &lines);
    return (SourceLines *) hashmap_get(source_files, &lines);
  }
}

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
     subtract one to access arrays. */
  for (
    unsigned cur_lineno = start_lineno;
    cur_lineno < end_lineno;
    cur_lineno++
  ) {
    if (cur_lineno == lineno) {
      /* Highlight current line. */
      fputs(" -> ", stdout);
    } else {
      fputs("    ", stdout);
    }

    /* The string read by `getline(3)` ends in a newline. */
    fputs(lines->lines[cur_lineno - 1], stdout);
  }

  return SP_OK;
}

