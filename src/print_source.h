#pragma once

#ifndef _SPRAY_PRINT_SOURCE_H_
#define _SPRAY_PRINT_SOURCE_H_

#include "magic.h"

typedef struct Sources Sources;

Sources *init_sources (void);

SprayResult print_source (Sources *sources, const char *filepath,
			  int lineno, int radius);

void free_sources (Sources *sources);

#endif /* _SPRAY_PRINT_SOURCE_H_ */
