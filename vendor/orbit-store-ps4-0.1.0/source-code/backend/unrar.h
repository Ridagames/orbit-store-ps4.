#ifndef ORBIT_UNRAR_H
#define ORBIT_UNRAR_H
#include "sevenzip.h"
/* RAR archives through RARLAB's UnRAR, with multithreaded unpacking. The same
 * contract as seven_inspect/seven_extract: UnRAR reads only fd, opens no paths
 * or volumes and writes nothing; every entry goes through the callbacks.
 * 0 success, 1 cancelled, -1 damaged/unsupported, -2 password missing/wrong,
 * -3 (unrar_extract only) an entry's window is over max_window: nothing was
 * written, and the caller can use another decoder. */
#include <stdint.h>
/* The largest window UnRAR keeps in memory on a PS4: as much as 7-Zip keeps
 * whole there (backend/sevenzip.cpp). Bigger ones go to 7-Zip, which keeps
 * them in a file on the drive. */
#define UNRAR_MEMORY_WINDOW ((uint64_t)128 << 20)
#ifdef __cplusplus
extern "C" {
#endif
int unrar_inspect(int fd, const char *password, const SevenCallbacks *callbacks, void *context,
                  char *error, size_t error_cap);
int unrar_extract(int fd, const char *password, unsigned threads, uint64_t max_window,
                  const SevenCallbacks *callbacks, void *context, char *error, size_t error_cap);
/* Threads to unpack with: the online CPUs, at most 4, at least 1. */
unsigned unrar_threads(void);
#ifdef __cplusplus
}
#endif
#endif
