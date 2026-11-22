#ifndef ORBIT_GOLDHEN_AUTORUN_H
#define ORBIT_GOLDHEN_AUTORUN_H
#include <stdbool.h>
#include <stddef.h>
/* Change only Orbit's entries in [AutoRun]; preserve other payloads and settings. */
int goldhen_autorun_edit(const char *input, size_t size, bool enable, char **output, size_t *length);
bool goldhen_autorun_enabled(const char *input, size_t size);
#endif
