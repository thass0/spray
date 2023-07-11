#include "history.h"

#include <assert.h>
#include <string.h>

struct History { char *command; };

History init_history(void) {
  History history = (History) calloc (1, sizeof(struct History));
  assert(history != NULL);
  return history;
}

void free_history(History history) {
  if (history != NULL) {
    free(history->command);
    free(history);
  }
}

void save_command(History history, const char *line) {
  assert(history != NULL);
  assert(line != NULL);

  history->command = (char *) realloc (history->command,
                                       strlen(line) + 1);
  strcpy(history->command, line);
}

SprayResult read_command(History history, char **line) {
  assert(history != NULL);
  assert(line != NULL);
  if (history->command == NULL) {
    return SP_ERR;
  } else {
    *line = strdup(history->command);
    return SP_OK;
  }
}
