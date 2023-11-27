#include "print_source.h"
#include "args.h"

#include <stdbool.h>
#include <tree_sitter/api.h>

TSLanguage *tree_sitter_c (void);

/* Highlight the given string and return a string that contains
 * ANSI escape characters to represent the colors. */
char *
highlight (const char *src)
{
  unused (src);
  TSParser *parser = ts_parser_new ();
  ts_parser_set_language(parser, tree_sitter_c ());
  return NULL;
}

SprayResult
print_source (const char *filepath, unsigned lineno, unsigned n_context)
{
  unused (filepath);
  unused (lineno);
  unused (n_context);

  bool use_color = !get_args ()->flags.no_color;
  unused (use_color);
  int res = 0;

  /* int res = print_source_extern (filepath, */
  /* 				 lineno, */
  /* 				 n_context, */
  /* 				 use_color); */
  if (res == 0)
    {
      return SP_OK;
    }
  else
    {
      return SP_ERR;
    }
}

void
init_print_source (void)
{
  ;
}
