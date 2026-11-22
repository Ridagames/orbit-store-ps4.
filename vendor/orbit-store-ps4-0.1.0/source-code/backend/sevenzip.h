#ifndef ORBIT_SEVENZIP_H
#define ORBIT_SEVENZIP_H
#include <stddef.h>
#include <stdint.h>
/* Entries in one archive, for inspection and extraction (backend/extract.c,
 * backend/unrar.cpp). */
#define ORBIT_ARCHIVE_MAX_ENTRIES 100000
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    /* check: 0 include, 2 skip clutter, otherwise abort. Called for ALL entries
     * before any output. begin/write/end only run for included entries. */
    int (*check)(void *, const char *, int64_t, int directory, int unsafe);
    int (*begin)(void *, const char *, int64_t, int directory);
    int (*write)(void *, const void *, size_t);
    int (*end)(void *);
    int (*stopped)(void *);
} SevenCallbacks;
/* No paths are opened by the decoder: it only reads fd and calls our writer.
 * 0 success, 1 cancelled, -1 damaged/unsupported, -2 password missing/wrong. */
int seven_inspect(int fd, const char *kind, const char *password,
                  const SevenCallbacks *callbacks, void *context, char *error, size_t error_cap);
int seven_extract(int fd, const char *kind, const char *password,
                  const SevenCallbacks *callbacks, void *context,
                  char *error, size_t error_cap);
/* Where a RAR5 window too large for memory may live, for this thread's next
 * extractions: a directory on the destination drive (-1: memory only) and how
 * many bytes of drive space to reserve for it (the archive's unpacked size). */
void seven_window_place(int directory, int64_t reserve);
#ifdef __cplusplus
}
#endif
#endif
