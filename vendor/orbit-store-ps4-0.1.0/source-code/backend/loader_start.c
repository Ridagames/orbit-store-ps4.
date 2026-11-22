/* Starting a payload through the console's ELF loaders; see loader_start.h. */
#if (defined(ORBIT_DESKTOP) || defined(ORBIT_STARTER_TEST)) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "loader_start.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void pause_ms(int ms) {
    struct timespec t = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) && errno == EINTR) {
    }
}

/* Milliseconds since `since` (set from the same clock). The waits below count real time,
 * so a connection attempt that itself takes long can't stretch them. */
static long elapsed_ms(const struct timespec *since) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - since->tv_sec) * 1000L + (now.tv_nsec - since->tv_nsec) / 1000000L;
}

#ifdef ORBIT_STARTER_TEST
/* Host tests: ORBIT_STARTER_NET_DOWN_MS makes every connection fail with ENETDOWN for
 * that long after the first one, like a console whose network is still starting. */
#include <stdlib.h>
static bool network_down(void) {
    static struct timespec first;
    static bool started;
    const char *value = getenv("ORBIT_STARTER_NET_DOWN_MS");
    if (!value || !*value)
        return false;
    if (!started) {
        clock_gettime(CLOCK_MONOTONIC, &first);
        started = true;
    }
    return elapsed_ms(&first) < atol(value);
}
#endif

int loader_connect_port(int port, int timeout_seconds) {
    if (port <= 0 || port > 65535) {
        errno = EINVAL;
        return -1;
    }
#ifdef ORBIT_STARTER_TEST
    if (network_down()) {
        errno = ENETDOWN;
        return -1;
    }
#endif
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval wait = {.tv_sec = timeout_seconds > 0 ? timeout_seconds : 10};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

bool loader_port_answers(int port) {
    int fd = loader_connect_port(port, 5);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

static int send_all(int fd, const unsigned char *data, size_t size) {
    for (size_t sent = 0; sent < size;) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, data + sent, size - sent, 0);
#endif
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* The loader runs what it read once this side closes; wait for it to finish reading. */
static int finish(int fd, int rc) {
    if (!rc) {
        shutdown(fd, SHUT_WR);
        char ignored[256];
        while (recv(fd, ignored, sizeof ignored, 0) > 0) {
        }
    }
    close(fd);
    return rc;
}

int loader_send_bytes(int fd, const unsigned char *data, size_t size) {
    if (fd < 0)
        return -1;
    return finish(fd, size && data ? send_all(fd, data, size) : -1);
}

static int open_regular(const char *path, off_t *size) {
    int file = open(path, O_RDONLY);
    struct stat st;
    if (file >= 0 && (fstat(file, &st) || !S_ISREG(st.st_mode) || st.st_size < 64)) {
        close(file);
        return -1;
    }
    if (file >= 0)
        *size = st.st_size;
    return file;
}

static int stream_file(int fd, int file) {
    static unsigned char buffer[64 * 1024];
    for (;;) {
        ssize_t got = read(file, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0)
            return -1;
        if (got == 0)
            return 0;
        if (send_all(fd, buffer, (size_t)got))
            return -1;
    }
}

int loader_send_file(int fd, const char *path) {
    if (fd < 0)
        return -1;
    off_t size = 0;
    int file = open_regular(path, &size);
    if (file < 0) {
        close(fd);
        return -1;
    }
    int rc = stream_file(fd, file);
    close(file);
    return finish(fd, rc);
}

int loader_start_connect_traced(int port, int goldhen_port, const char *elfldr_path, int wait_ms,
                                LoaderStartTrace *trace) {
    LoaderStartTrace ignored;
    if (!trace)
        trace = &ignored;
    *trace = (LoaderStartTrace){.first_errno = -1, .goldhen_errno = -1, .loader_errno = -1};
    struct timespec step;
    /* The ELF loader itself. Only a plain refusal (nothing listens) leads to GoldHEN;
     * other errors are retried, as the network may still be starting at boot. */
    clock_gettime(CLOCK_MONOTONIC, &step);
    int fd;
    for (;;) {
        fd = loader_connect_port(port, 10);
        trace->first_errno = fd >= 0 ? 0 : errno;
        trace->first_ms = (int)elapsed_ms(&step);
        if (fd >= 0 || errno == ECONNREFUSED || errno == EINVAL || trace->first_ms >= wait_ms)
            break;
        pause_ms(500);
    }
    if (fd >= 0 || trace->first_errno != ECONNREFUSED || goldhen_port <= 0 || !elfldr_path) {
        errno = fd >= 0 ? 0 : trace->first_errno;
        return fd;
    }
    /* Read elfldr before touching GoldHEN's loader: never connect there without a file. */
    off_t size = 0;
    int file = open_regular(elfldr_path, &size);
    if (file < 0) {
        errno = ENOENT;
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &step);
    int goldhen;
    for (;;) {
        goldhen = loader_connect_port(goldhen_port, 10);
        trace->goldhen_errno = goldhen >= 0 ? 0 : errno;
        trace->goldhen_ms = (int)elapsed_ms(&step);
        if (goldhen >= 0 || errno == EINVAL || trace->goldhen_ms >= wait_ms)
            break;
        pause_ms(500);
    }
    if (goldhen < 0) {
        close(file);
        errno = trace->goldhen_errno;
        return -1;
    }
    int rc = finish(goldhen, stream_file(goldhen, file));
    close(file);
    if (rc) {
        trace->goldhen_errno = EIO;
        errno = EIO;
        return -1;
    }
    trace->elfldr_sent = true;
    clock_gettime(CLOCK_MONOTONIC, &step);
    for (;;) {
        fd = loader_connect_port(port, 10);
        trace->loader_errno = fd >= 0 ? 0 : errno;
        trace->loader_ms = (int)elapsed_ms(&step);
        if (fd >= 0 || errno == EINVAL || trace->loader_ms >= wait_ms)
            break;
        pause_ms(250);
    }
    errno = fd >= 0 ? 0 : trace->loader_errno;
    return fd;
}

int loader_start_connect(int port, int goldhen_port, const char *elfldr_path, int wait_ms,
                         bool *sent_elfldr) {
    LoaderStartTrace trace;
    int fd = loader_start_connect_traced(port, goldhen_port, elfldr_path, wait_ms, &trace);
    if (sent_elfldr)
        *sent_elfldr = trace.elfldr_sent;
    return fd;
}
