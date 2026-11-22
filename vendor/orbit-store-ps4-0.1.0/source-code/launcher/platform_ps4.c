#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif
/* A real PS4 PKG installs the browser shortcut. Never scan/register arbitrary
 * /user/app folders or write a PS5 param.json-only tile into the PS4 app DB. */
#include "install.h"
#include "packages.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern const unsigned char orbit_launcher_pkg[], orbit_launcher_pkg_end[];
#define TITLE "ORBT00001"
#define CONTENT "UP9000-" TITLE "_00-ORBITBROWSER0001"
#define PACKAGE "browser-shortcut.pkg"
#define INTENT "browser-shortcut-task-v2"
/* launcher/sce_sys/param.json appVersion; tests/launcher_platform.py checks it.
 * 01.01 adds the provider-page helper (Sony's browser window); 01.02 closes
 * the window when the session ends in Orbit; 01.04 opens the window the
 * service names: the custom window (Cross does nothing) for Vikingfile pages.
 * 01.03 was a console test build only. */
#define HELPER_VERSION "01.04"

const char *orbit_launcher_registration_method(void) { return "bgft-storage"; }
static int fail(const char *step, int code) {
    orbit_launcher_record_error(step, errno, code); return -1;
}
/* Private state directory is already locked by the running service. */
static int stage(int parent) {
    size_t size = (size_t)(orbit_launcher_pkg_end - orbit_launcher_pkg);
    if (size < 4096 || memcmp(orbit_launcher_pkg, "\177CNT", 4)) return fail("shortcut-package-invalid", -1);
    int fd = openat(parent, PACKAGE, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd >= 0) {
        struct stat st; unsigned char buffer[16384]; size_t pos = 0;
        bool same = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size == (off_t)size;
        while (same && pos < size) {
            size_t count = size - pos < sizeof buffer ? size - pos : sizeof buffer;
            ssize_t n = read(fd, buffer, count);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0 || memcmp(buffer, orbit_launcher_pkg + pos, (size_t)n)) same = false;
            else pos += (size_t)n;
        }
        close(fd);
        return same ? 0 : fail("shortcut-stage-changed", -1);
    }
    if (errno != ENOENT) return fail("shortcut-stage-open", -1);
    fd = openat(parent, PACKAGE, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return fail("shortcut-stage-create", -1);
    size_t pos = 0;
    while (pos < size) {
        ssize_t n = write(fd, orbit_launcher_pkg + pos, size - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        pos += (size_t)n;
    }
    int rc = pos == size ? fsync(fd) : -1;
    if (close(fd)) rc = -1;
    if (rc) return fail("shortcut-stage-write", rc);
    return 0;
}
/* APP_VER of the installed copy, from the console's application metadata. */
static bool installed_version(char out[8]) {
    char path[1200];
    snprintf(path, sizeof path, "%s/system_data/priv/appmeta/" TITLE "/param.sfo", orbit.system_root);
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    unsigned char data[8192];
    ssize_t used = read(fd, data, sizeof data);
    close(fd);
    if (used < 20 || memcmp(data, "\0PSF", 4)) return false;
#define U16(p) ((unsigned)(p)[0] | (unsigned)(p)[1] << 8)
#define U32(p) ((unsigned)U16(p) | (unsigned)U16((p) + 2) << 16)
    size_t keys = U32(data + 8), values = U32(data + 12), count = U32(data + 16);
    if (keys > (size_t)used || values > (size_t)used || count > ((size_t)used - 20) / 16) return false;
    for (size_t i = 0; i < count; i++) {
        const unsigned char *e = data + 20 + i * 16;
        size_t key = keys + U16(e), length = U32(e + 4), at = values + U32(e + 12);
        if (key >= (size_t)used || !memchr(data + key, 0, (size_t)used - key) ||
            strcmp((const char *)data + key, "APP_VER")) continue;
        if (U16(e + 2) != 0x0204 || length < 6 || at + 6 > (size_t)used) return false;
        memcpy(out, data + at, 5); out[5] = 0;
        return true;
    }
#undef U16
#undef U32
    return false;
}
int orbit_launcher_ensure(int state_fd) {
    bool installed = false;
    int rc = package_platform_installed(TITLE, &installed);
    if (rc) return fail("shortcut-query", rc);
    if (installed) {
        /* An older helper cannot open provider pages: replace it. It holds no
         * user data. Unknown or newer versions are left alone. */
        char version[8];
        if (!installed_version(version) || strcmp(version, HELPER_VERSION) >= 0) return 0;
        rc = package_platform_uninstall(TITLE);
        if (rc) return fail("shortcut-uninstall", rc);
        rc = package_platform_installed(TITLE, &installed);
        if (rc || installed) return fail("shortcut-uninstall", rc ? rc : -1);
    }
    char task[49] = {0};
    int fd = openat(state_fd, INTENT, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd >= 0) {
        struct stat st;
        bool valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 5 && st.st_size < 49;
        ssize_t n = valid ? read(fd, task, (size_t)st.st_size) : -1;
        close(fd);
        if (!valid || n != st.st_size || strncmp(task, "bgft:", 5))
            return fail("shortcut-install-unconfirmed", -1);
    } else {
        if (errno != ENOENT) return fail("shortcut-intent-open", -1);
        if (stage(state_fd)) return -1;
        /* Save intent before submission. A crash can never silently submit twice. */
        fd = openat(state_fd, INTENT, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) return fail("shortcut-intent-create", -1);
        bool saved = write(fd, "pending", 7) == 7 && !fsync(fd) && !fsync(state_fd);
        if (!saved) { close(fd); return fail("shortcut-intent-save", -1); }
        char path[1100], error[256] = {0};
        snprintf(path, sizeof path, "%s/" PACKAGE, orbit.state_dir);
        rc = package_platform_submit(path, "Orbit Store Browser", CONTENT, task, error, sizeof error);
        if (rc) {
            close(fd); unlinkat(state_fd, INTENT, 0); fsync(state_fd);
            printf("Orbit browser shortcut: %s\n", error);
            return fail("shortcut-submit", rc);
        }
        saved = *task && !ftruncate(fd, 0) && lseek(fd, 0, SEEK_SET) == 0 &&
                write(fd, task, strlen(task)) == (ssize_t)strlen(task) && !fsync(fd);
        close(fd);
        if (!saved) return fail("shortcut-task-save", -1);
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        PackageProgress progress;
        rc = package_platform_poll(task, &progress);
        if (rc) return fail("shortcut-install-unconfirmed", rc);
        if (!strcmp(progress.state, "error")) {
            unlinkat(state_fd, INTENT, 0); fsync(state_fd);
            return fail("shortcut-install", progress.error);
        }
        if (!strcmp(progress.state, "completed")) {
            rc = package_platform_installed(TITLE, &installed);
            if (!rc && installed) {
                unlinkat(state_fd, INTENT, 0); unlinkat(state_fd, PACKAGE, 0); fsync(state_fd);
                return 1;
            }
        }
        nanosleep(&(struct timespec){.tv_nsec = 100000000}, NULL);
    }
    return 2; /* queued; subsequent startup queries the same task, never resubmits */
}
