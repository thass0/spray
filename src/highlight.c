#include "highlight.h"

#include <string.h>
#include <tree_sitter/api.h>

TSLanguage *tree_sitter_c (void);

/* Highlight the given string and return a string that contains
 * ANSI escape characters to represent the colors. */
char *
highlight (const char *src)
{
  /* TSParser *parser = ts_parser_new (); */
  /* ts_parser_set_language(parser, tree_sitter_c ()); */

  /* TODO: Highlight */
  return strdup (src);
}
