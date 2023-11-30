/* Syntax highlight C source code. */

#pragma once

#ifndef _SPRAY_HIGHLIGHT_H_
#define _SPRAY_HIGHLIGHT_H_

/* Highlight the given C source code. Allocates and returns
 * a new string that contains ANSI escape sequences that,
 * when printed to a shell, turn into colors. */
char *highlight (const char *src);

#endif	/* _SPRAY_HIGHLIGHT_H_ */

