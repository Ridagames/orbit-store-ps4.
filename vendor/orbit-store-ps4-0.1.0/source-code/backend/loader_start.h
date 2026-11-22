/* Starting a payload through the console's ELF loaders.
 *
 * Orbit runs through elfldr (port 9021). GoldHEN's own payload loader (9090) and
 * LaunchPad start small programs but not one of Orbit's size, so when nothing
 * answers on 9021 Orbit sends its bundled copy of elfldr to 9090 first, waits for
 * 9021 and sends the payload there. Shared by the service (restarts), the starter
 * that payload managers run (launcher/starter.c) and the full payload's first start.
 * Never opens a connection to GoldHEN's loader without sending a whole file: an
 * empty connection wedges it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define ORBIT_ELF_LOADER_PORT 9021
#define ORBIT_GOLDHEN_LOADER_PORT 9090
#define ORBIT_ELFLDR_NAME "elfldr.elf"

/* A connection to 127.0.0.1:port, or -1 with errno set (ECONNREFUSED: nothing listens). */
int loader_connect_port(int port, int timeout_seconds);

/* A connection to the ELF loader on `port`. When nothing listens there and
 * goldhen_port > 0, sends the elfldr at elfldr_path once to goldhen_port (a failed
 * connection is retried for up to wait_ms, as GoldHEN may still be starting), then
 * waits up to wait_ms for `port` to accept. Other errors than a refusal on `port` are
 * retried for up to wait_ms before giving up (the network may still be starting). -1 when no loader could be reached.
 * *sent_elfldr (may be NULL) says whether elfldr was sent. */
int loader_start_connect(int port, int goldhen_port, const char *elfldr_path, int wait_ms,
                         bool *sent_elfldr);

/* What loader_start_connect_traced saw, for the starter's log. An errno of 0 means
 * the connection was made; -1 means that step wasn't reached. */
typedef struct {
    int first_errno;   /* the first try on `port` (after any retries) */
    int first_ms;
    int goldhen_errno; /* the last try on goldhen_port; EIO if sending elfldr there failed */
    int goldhen_ms;
    bool elfldr_sent;
    int loader_errno;  /* the last try on `port` after sending elfldr */
    int loader_ms;
} LoaderStartTrace;

/* loader_start_connect, recording each step in *trace (may be NULL). Any connection
 * error, not only "refused", is retried for up to wait_ms on each port: early in a
 * boot the console's network may not be ready. */
int loader_start_connect_traced(int port, int goldhen_port, const char *elfldr_path, int wait_ms,
                                LoaderStartTrace *trace);

/* Streams `size` bytes to a loader connection, then lets the loader finish reading:
 * shuts the sending side and waits for the loader to close. Always closes fd. 0 on success. */
int loader_send_bytes(int fd, const unsigned char *data, size_t size);

/* The same for a file, read in 64 KB pieces (never held whole in memory). */
int loader_send_file(int fd, const char *path);

/* Whether something accepts connections on 127.0.0.1:port (used for Orbit's own 34177). */
bool loader_port_answers(int port);
