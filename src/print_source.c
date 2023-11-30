#include "print_source.h"

#include "args.h"
#include "highlight.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <hashmap.h>

#define TRUE 7
#define FALSE 0

/**********************************/
/* Source file and hash map logic */
/**********************************/

struct Sources
{
  struct hashmap *map;
};

typedef struct
{
  char *filepath;
  char *text;
  int n_lines;
} SourceFile;

int
file_compare (const void *a, const void *b, void *user_data)
{
  unused (user_data);
  const SourceFile *file_a = a;
  const SourceFile *file_b = b;

  return strcmp (file_a->filepath, file_b->filepath);
}

uint64_t file_hash (const void *entry, uint64_t seed0, uint64_t seed1)
{
  const SourceFile *file = entry;
  return hashmap_sip (file->filepath, strlen (file->filepath), seed0, seed1);
}

Sources *init_sources (void)
{
  struct hashmap *map = hashmap_new (sizeof (SourceFile), 0, 0, 0,
				     file_hash, file_compare,
				     NULL, NULL);
  Sources *sources = malloc (sizeof (Sources));
  sources->map = map;
  return sources;
}

void
free_sources (Sources *sources)
{
  assert (sources != NULL);

  /* Free all entries. */
  size_t iter = 0;
  void *item;
  while (hashmap_iter (sources->map, &iter, &item))
    {
      const SourceFile *file = item;
      free (file->filepath);
      free (file->text);
    }
  
  hashmap_free (sources->map);
  free (sources);
}


/***********************/
/* Manage file content */
/***********************/

int
count_lines (char *text)
{
  int n = 1;
  while ((text = strchr (text, '\n')))
    {
      text ++, n++;
    }
  return n;
}

/* Return a pointer to the start of the nth line
 * in the given string. The first line (the start
 * of the string itself) has the line number 1. */
char *
nth_line (int n, char *text)
{
  if (n < 1 || text == NULL)
    return NULL;

  if (n == 1)
    return text;

  int c = 1;
  while ((text = strchr (text, '\n')))
    {
      text ++;			/* Skip the '\n' itself. */
      c ++;
      if (c == n)
	return text;
    }

  return strchr (text, '\0');
}

char *
read_file (const char *filepath)
{
  if (filepath == NULL)
    return NULL;

  FILE *fp = fopen (filepath,  "r");
  if (fp == NULL)
    return NULL;

  if (fseek (fp, 0, SEEK_END))
    goto exit_fp;

  long n = ftell (fp);
  if (n == -1)
    goto exit_fp;

  if (fseek (fp, 0, SEEK_SET))
    goto exit_fp;

  char *text = malloc (n + 1);
  if (text == NULL)
    goto exit_fp;

  fread (text, n, 1, fp);
  fclose (fp);
  text[n] = '\0';
  return text;

 exit_fp:
  fclose (fp);
  return NULL;
}

const SourceFile *
load_file (Sources *sources, const char *filepath)
{
  if (sources == NULL || filepath == NULL)
    return NULL;


  char *text = read_file (filepath);
  if (text == NULL)
    return NULL;

  /* Color the source code if needed. */
  bool use_color = !get_args ()->flags.no_color;
  if (use_color)
    {
      char *highlighted = highlight (text);
      free (text);
      text = highlighted;
    }

  /* Store the loaded file. */  
  SourceFile file = {0};
  file.filepath = strdup (filepath);
  file.text = text;
  file.n_lines = count_lines (text);

  hashmap_set (sources->map, &file);
  return hashmap_get (sources->map, &file);
}

const SourceFile *
get_or_load_file (Sources *sources, const char *filepath)
{
  if (sources == NULL || filepath == NULL)
    return NULL;

  SourceFile lookup = {0};
  lookup.filepath = strdup (filepath);  
  const SourceFile *file = hashmap_get (sources->map, &lookup);
  free (lookup.filepath);

  if (file == NULL)
      return load_file (sources, filepath);
  else
      return file;
}


/*********************************/
/* Print snippets of source code */
/*********************************/

int
start_lineno (int lineno, int radius)
{
  if (lineno > radius)
      return lineno - radius;
  else
      return 1;
}

int
end_lineno (int lineno, int radius)
{
  /* The window is extended downwards if there
   * are not enough lines available above. */
  int extend = 0;
  if (lineno < radius)
      extend = radius - lineno;

  return lineno + radius + 1 + extend;
}

/* Return `true` if `line` contains characters
 * that are visible to the eye. */
int
is_visible (char *line)
{
  if (line == NULL)
    return FALSE;

  int c = 0;
  for (int i = 0; line[i] != '\0'; i++)
    {
      c = line[i];
      if (isgraph (c))
	return TRUE;
    }

  return FALSE;
}


void
print_lines (char *text, int n_lines, int lineno, int radius)
{
  int start_raw = start_lineno (lineno, radius);
  int end_raw = end_lineno (lineno, radius);

  int start, end;
  
  if (start_raw >= n_lines)
    start = n_lines - 1;
  else
    start = start_raw;

  if (end_raw > n_lines)
    end = n_lines;
  else
    end = end_raw;

  for (int i = start; i < end; i++)
    {
      char *this_line = nth_line (i, text);
      char *next_line = nth_line (i + 1, text);
      char save = next_line[0];
      next_line[0] = '\0';

      printf (" %4d", i);
      if (i == lineno)
	printf (" -> %s", this_line);
      else if (is_visible (this_line))
	printf ("    %s", this_line);
      else
	printf ("\n");

      next_line[0] = save;
    }
}

SprayResult
print_source (Sources *sources, const char *filepath, int lineno, int radius)
{
  if (sources == NULL || filepath == NULL)
    return SP_ERR;

  const SourceFile *file = get_or_load_file (sources, filepath);
  if (file == NULL)
    {      
      return SP_ERR;
    }
  else
    {
      print_lines (file->text, file->n_lines, lineno, radius);
      return SP_OK;
    }
}
