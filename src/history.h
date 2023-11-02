#pragma once

#ifndef _SPRAY_HISTORY_H_
#define _SPRAY_HISTORY_H_

#include "magic.h"

#include <stdlib.h>

typedef struct History* History;

History init_history(void);

void free_history(History history);

void save_command(History history, const char *line);

/* Copys the command to `line` (if there is a command)
 * using `malloc`. The caller must free the copy. */
SprayResult read_command(History history, char **line);

#endif  /* _SPRAY_HISTORY_H_ */
