/* Orbit's starter: the small program GoldHEN's LaunchPad and AutoRun (and Homebrew
 * Launcher) run in place of the service, which is too big for GoldHEN's loader.
 *
 * 1. Orbit already answers on 34177: say so and stop.
 * 2. Send the saved service (/data/orbit-store/orbit_store.elf) to elfldr on 9021. When
 *    nothing answers there, first send the saved elfldr (/data/orbit-store/elfldr.elf) to
 *    GoldHEN's payload loader on 9090, wait for 9021 and 3 s more (backend/loader_start.c).
 * 3. Wait for Orbit to answer. If it hasn't after RETRY_AFTER_MS and the loader still
 *    answers, send it once more: on a PS4 (12.00, 13 Nov 2026) the first payload a just-
 *    started elfldr ran sometimes died at once (3 of 10 tries); the next send always worked.
 *    A second copy that finds Orbit running only says so and exits.
 * 4. If Orbit still doesn't answer, say what usually stops it.
 *
 * Each step goes to /data/orbit-store/starter.log (started afresh once it passes 32 KB)
 * and the kernel log, so a start that fails at boot can be explained afterwards.
 *
 * Runs inside GoldHEN's own process when started from LaunchPad, so it stays small: no
 * allocations, files streamed in 64 KB pieces. Messages are console notifications.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#if defined(ORBIT_STARTER_TEST) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "loader_start.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ORBIT_PORT 34177

#ifdef ORBIT_STARTER_TEST
/* Host tests: paths, ports and waits from the environment; notifications to a file. */
static const char *env(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}
#define SERVICE env("ORBIT_STARTER_SERVICE", "")
#define ELFLDR env("ORBIT_STARTER_ELFLDR", "")
#define PORT_ORBIT atoi(env("ORBIT_STARTER_ORBIT_PORT", "0"))
#define PORT_LOADER atoi(env("ORBIT_STARTER_LOADER_PORT", "0"))
#define PORT_GOLDHEN atoi(env("ORBIT_STARTER_GOLDHEN_PORT", "0"))
#define LOADER_WAIT_MS atoi(env("ORBIT_STARTER_WAIT_MS", "3000"))
#define ORBIT_WAIT_MS atoi(env("ORBIT_STARTER_ORBIT_WAIT_MS", "3000"))
#define RETRY_AFTER_MS atoi(env("ORBIT_STARTER_RETRY_MS", "1500"))
static void notify(const char *text) {
    FILE *f = fopen(env("ORBIT_STARTER_NOTIFY", "/dev/null"), "a");
    if (f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}
#define LOG_PATH env("ORBIT_STARTER_LOG", "")
static void klog_line(const char *line) {
    (void)line;
}
#else
#include <ps4/klog.h>
#define LOG_PATH "/data/orbit-store/starter.log"
static void klog_line(const char *line) {
    klog_printf("%s", line);
}
#define SERVICE "/data/orbit-store/orbit_store.elf"
#define ELFLDR "/data/orbit-store/" ORBIT_ELFLDR_NAME
#define PORT_ORBIT ORBIT_PORT
#define PORT_LOADER ORBIT_ELF_LOADER_PORT
#define PORT_GOLDHEN ORBIT_GOLDHEN_LOADER_PORT
/* At boot GoldHEN's loader and elfldr may take a while to listen. */
#define LOADER_WAIT_MS 30000
#define ORBIT_WAIT_MS 90000
/* Orbit answers 11-20 s after it is sent; a copy still starting gets 40 s. */
#define RETRY_AFTER_MS 40000
extern int sceKernelSendNotificationRequest(int, const void *, size_t, int);
static void notify(const char *text) {
    static unsigned char notification[3120];
    memset(notification, 0, sizeof notification);
    snprintf((char *)notification + 45, sizeof notification - 45, "%s", text);
    sceKernelSendNotificationRequest(0, notification, sizeof notification, 0);
}
#endif

static int log_fd = -1;
static struct timespec started;

/* One line of the log: seconds since the starter began, then the text. */
static void say(const char *format, ...) {
    static char line[320];
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (long)(now.tv_sec - started.tv_sec) * 1000L + (now.tv_nsec - started.tv_nsec) / 1000000L;
    int n = snprintf(line, sizeof line, "[ORBIT-START] +%ld.%ld s ", ms / 1000, (ms % 1000) / 100);
    va_list args;
    va_start(args, format);
    vsnprintf(line + n, sizeof line - (size_t)n - 1, format, args);
    va_end(args);
    size_t length = strlen(line);
    line[length++] = '\n';
    line[length] = 0;
    if (log_fd >= 0 && write(log_fd, line, length) < 0) {
        close(log_fd);
        log_fd = -1;
    }
    klog_line(line);
}

static void log_open(void) {
    clock_gettime(CLOCK_MONOTONIC, &started);
    const char *path = LOG_PATH;
    if (*path) {
        struct stat st;
        int flags = O_WRONLY | O_CREAT | O_APPEND;
        if (!stat(path, &st) && st.st_size > 32 * 1024)
            flags |= O_TRUNC;
        log_fd = open(path, flags, 0644);
    }
    time_t now = time(NULL);
    struct tm utc;
    char when[32] = "unknown time";
    if (now > 0 && gmtime_r(&now, &utc))
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", &utc);
    say("starter began, %s", when);
}

/* A connection result in words. */
static const char *outcome(int error) {
    switch (error) {
    case 0:
        return "connected";
    case ECONNREFUSED:
        return "refused, nothing listens";
    case ETIMEDOUT:
        return "timed out";
    case ENETDOWN:
        return "network down";
    case ENETUNREACH:
        return "network unreachable";
    case EHOSTUNREACH:
        return "host unreachable";
    case EADDRNOTAVAIL:
        return "address not available";
    case EIO:
        return "sending failed";
    default:
        return "failed";
    }
}

static void say_try(const char *what, int port, int error, int ms) {
    say("%s (%d): %s (errno %d), %d.%d s of retries", what, port, outcome(error), error, ms / 1000,
        (ms % 1000) / 100);
}

static bool readable(const char *path) {
    struct stat st;
    return path && *path && !stat(path, &st) && S_ISREG(st.st_mode) && st.st_size >= 64;
}

static bool orbit_answers_within(int ms) {
    for (int waited = 0;; waited += 500) {
        if (loader_port_answers(PORT_ORBIT))
            return true;
        if (waited >= ms)
            return false;
        struct timespec t = {.tv_nsec = 500000000L};
        nanosleep(&t, NULL);
    }
}

int main(void) {
    log_open();
    if (loader_port_answers(PORT_ORBIT)) {
        say("Orbit already answers on %d", PORT_ORBIT);
        notify("Orbit Store is already running. Open its home-screen icon.");
        return 0;
    }
    if (!readable(SERVICE)) {
        say("saved copy missing: %s", SERVICE);
        notify("Orbit's saved copy is missing. Run orbit_store.elf again.");
        return 1;
    }
    bool have_elfldr = readable(ELFLDR);
    say("saved copy found; saved elfldr %s", have_elfldr ? "found" : "missing");
    LoaderStartTrace trace;
    int loader = loader_start_connect_traced(PORT_LOADER, PORT_GOLDHEN, have_elfldr ? ELFLDR : NULL,
                                             LOADER_WAIT_MS, &trace);
    say_try("ELF loader", PORT_LOADER, trace.first_errno, trace.first_ms);
    if (trace.goldhen_errno >= 0)
        say_try("GoldHEN payload loader", PORT_GOLDHEN, trace.goldhen_errno, trace.goldhen_ms);
    if (trace.elfldr_sent)
        say("elfldr sent to GoldHEN");
    if (trace.loader_errno >= 0)
        say_try("ELF loader after elfldr", PORT_LOADER, trace.loader_errno, trace.loader_ms);
    if (loader < 0) {
        say("gave up: no ELF loader");
        notify("Orbit couldn't start: no ELF loader answers on port 9021, and GoldHEN's payload "
               "loader (9090) didn't start Orbit's elfldr. Check that GoldHEN's PayLoader Server is on "
               "(GoldHEN, Servers Settings), or start an ELF loader, then try again.");
        return 1;
    }
    if (loader_send_file(loader, SERVICE)) {
        say("sending Orbit failed part way");
        notify("The ELF loader stopped taking Orbit part way.");
        return 1;
    }
    say("Orbit sent");
    if (!orbit_answers_within(RETRY_AFTER_MS)) {
        if (!loader_port_answers(PORT_LOADER)) {
            say("Orbit not answering; the ELF loader no longer answers either");
        } else {
            say("Orbit not answering; sending it once more");
            if (loader_send_file(loader_connect_port(PORT_LOADER, 10), SERVICE)) {
                say("sending Orbit again failed part way");
                notify("The ELF loader stopped taking Orbit part way.");
                return 1;
            }
        }
    }
    if (!orbit_answers_within(ORBIT_WAIT_MS)) {
        say("gave up: Orbit never answered on %d", PORT_ORBIT);
        notify("Orbit didn't start. Close other apps such as ezRemote, then start Orbit again.");
        return 1;
    }
    say("Orbit answers on %d", PORT_ORBIT);
    return 0;
}
