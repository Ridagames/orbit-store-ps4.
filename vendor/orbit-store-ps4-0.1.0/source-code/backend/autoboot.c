/* Saved runtime copy and opt-in payload-manager integration. External copies require
 * separate user choices. On PS4 the manager target is GoldHEN's LaunchPad: payloads
 * are dropped as plain ELF files under /data/payloads. GoldHEN AutoRun is a
 * separate opt-in entry in payloads.ini (GoldHEN v2.4b18.10+). Paths are relative to
 * orbit.system_root so desktop tests can use a disposable tree; on the console it is empty.
 * Managers get the small starter (launcher/starter.c), not the service: GoldHEN's loader
 * can't run a program of Orbit's size. The starter sends the saved service to elfldr,
 * starting the bundled elfldr (saved beside the service) through GoldHEN first if needed. */
#include "launcher.h"
#include "orbit.h"
#include "goldhen_autorun.h"
#include "loader_start.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#define SAVED_NAME "orbit_store.elf"
#define ORBIT_DIR "/data/orbit-store"
#define SAVED ORBIT_DIR "/" SAVED_NAME
#define GOLDHEN_DIR "/data/GoldHEN"
#define PAYLOADS_DIR "/data/payloads"
#define PAYLOADS_COPY PAYLOADS_DIR "/" SAVED_NAME
#define AUTORUN GOLDHEN_DIR "/payloads.ini"
#define HOMEBREW_DIR "/data/homebrew"
#define HOMEBREW_APP HOMEBREW_DIR "/OrbitStore"
#define SAVED_ELFLDR ORBIT_DIR "/" ORBIT_ELFLDR_NAME
#define MAX_IMAGE_BYTES (64 * 1024 * 1024)
typedef char Path[1100];

static void at(char *out, const char *path) {
    snprintf(out, sizeof(Path), "%s%s", orbit.system_root, path);
}
static bool is_dir(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISDIR(st.st_mode);
}
static bool is_file(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode);
}
static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) && errno != EEXIST)
        return -1;
    return is_dir(path) ? 0 : -1;
}
/* Regular files only, never through a symlink. */
int read_regular_file(const char *path, size_t max, unsigned char **data, size_t *length) {
    *data = NULL;
    *length = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > max) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    size_t n = (size_t)st.st_size, p = 0;
    unsigned char *buffer = malloc(n + 1);
    while (buffer && p < n) {
        ssize_t got = read(fd, buffer + p, n - p);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        p += (size_t)got;
    }
    close(fd);
    if (!buffer || p != n) {
        free(buffer);
        errno = EIO;
        return -1;
    }
    buffer[n] = 0;
    *data = buffer;
    *length = n;
    return 0;
}
/* 1 identical, 0 missing or different, -1 not a regular file or unreadable. */
static int same_content(const char *path, const unsigned char *data, size_t length) {
    struct stat st;
    if (lstat(path, &st))
        return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode))
        return -1;
    if ((uint64_t)st.st_size != length)
        return 0;
    unsigned char *old;
    size_t n;
    if (read_regular_file(path, length, &old, &n))
        return -1;
    int same = n == length && !memcmp(old, data, length);
    free(old);
    return same;
}
static int write_atomic(const char *path, const unsigned char *data, size_t length, mode_t mode) {
    Path temp;
    snprintf(temp, sizeof temp, "%s.orbit-tmp", path);
    unlink(temp);
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (fd < 0)
        return -1;
    size_t p = 0;
    int rc = 0;
    while (p < length) {
        ssize_t w = write(fd, data + p, length - p);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0) {
            rc = -1;
            break;
        }
        p += (size_t)w;
    }
    if (!rc)
        rc = fsync(fd);
    if (close(fd))
        rc = -1;
    if (!rc)
        rc = rename(temp, path);
    if (rc)
        unlink(temp);
    return rc;
}
/* 1 written, 0 already current, -1 refused or failed. */
static int ensure_copy(const char *path, const unsigned char *data, size_t length, mode_t mode) {
    int same = same_content(path, data, length);
    if (same)
        return same > 0 ? 0 : -1;
    return write_atomic(path, data, length, mode) ? -1 : 1;
}
static int saved_image(unsigned char **data, size_t *length) {
    Path p;
    at(p, SAVED);
    if (read_regular_file(p, MAX_IMAGE_BYTES, data, length))
        return -1;
    if (*length < 4 || memcmp(*data, "\177ELF", 4)) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}
int autoboot_saved_image(unsigned char **data, size_t *length) {
    return saved_image(data, length);
}
#ifdef ORBIT_INSTALL_LAUNCHER
/* launcher/loader_images.c */
extern const unsigned char orbit_elfldr_image[], orbit_elfldr_image_end[];
extern const unsigned char orbit_starter_image[], orbit_starter_image_end[];
#endif
/* The starter (true) or elfldr (false) this build carries; none in desktop builds. Tests
 * name files instead (ORBIT_TEST_STARTER_IMAGE, ORBIT_TEST_ELFLDR_IMAGE). */
static bool bundled(bool starter, const unsigned char **data, size_t *length) {
#ifdef ORBIT_INSTALL_LAUNCHER
    *data = starter ? orbit_starter_image : orbit_elfldr_image;
    *length = (size_t)(starter ? orbit_starter_image_end - orbit_starter_image
                               : orbit_elfldr_image_end - orbit_elfldr_image);
    return *length > 0;
#elif defined(ORBIT_TEST)
    static unsigned char *images[2];
    static size_t sizes[2];
    const char *path = getenv(starter ? "ORBIT_TEST_STARTER_IMAGE" : "ORBIT_TEST_ELFLDR_IMAGE");
    if (!path)
        return false;
    if (!images[starter] && read_regular_file(path, MAX_IMAGE_BYTES, &images[starter], &sizes[starter]))
        return false;
    *data = images[starter];
    *length = sizes[starter];
    return true;
#else
    (void)starter;
    *data = NULL;
    *length = 0;
    return false;
#endif
}
void autoboot_elfldr_path(char *out, size_t cap) {
    snprintf(out, cap, "%s%s", orbit.system_root, SAVED_ELFLDR);
}
/* Saves the bundled elfldr beside the service, for restarts and the starter. */
static int save_elfldr(void) {
    const unsigned char *data;
    size_t length;
    if (!bundled(false, &data, &length))
        return 0;
    Path p;
    at(p, SAVED_ELFLDR);
    return ensure_copy(p, data, length, 0755) < 0 ? -1 : 0;
}
/* What a payload manager runs: the starter when this build has one, else the service. */
static void manager_copy(const unsigned char *service, size_t service_length,
                         const unsigned char **data, size_t *length) {
    if (!bundled(true, data, length)) {
        *data = service;
        *length = service_length;
    }
}
static int lock_settings(void) {
    Path p;
    at(p, ORBIT_DIR);
    if (ensure_dir(p))
        return -1;
    at(p, ORBIT_DIR "/autoboot.lock");
    int fd = open(p, O_WRONLY | O_CREAT | O_NOFOLLOW, 0600);
    if (fd >= 0 && flock(fd, LOCK_EX)) {
        close(fd);
        fd = -1;
    }
    return fd;
}

typedef struct { bool payload_manager, homebrew_launcher; } Integrations;
/* Absence means no consent, including on upgrades from versions that copied automatically. */
static int integrations_read(Integrations *settings) {
    memset(settings, 0, sizeof *settings);
    Path path;
    at(path, ORBIT_DIR "/integrations.json");
    unsigned char *data; size_t length;
    if (read_regular_file(path, 4096, &data, &length))
        return errno == ENOENT ? 0 : -1;
    cJSON *json = cJSON_ParseWithLengthOpts((const char *)data, length + 1, NULL, true);
    free(data);
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schemaVersion"),
          *pm = cJSON_GetObjectItemCaseSensitive(json, "payloadManager"),
          *hb = cJSON_GetObjectItemCaseSensitive(json, "homebrewLauncher");
    bool valid = cJSON_IsObject(json) && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
                 cJSON_IsBool(pm) && cJSON_IsBool(hb);
    if (valid) {
        settings->payload_manager = cJSON_IsTrue(pm);
        settings->homebrew_launcher = cJSON_IsTrue(hb);
    }
    cJSON_Delete(json);
    return valid ? 0 : -1;
}
static int integrations_save(const Integrations *settings) {
    Path path;
    at(path, ORBIT_DIR "/integrations.json");
    char data[128];
    int n = snprintf(data, sizeof data,
        "{\"schemaVersion\":1,\"payloadManager\":%s,\"homebrewLauncher\":%s}\n",
        settings->payload_manager ? "true" : "false", settings->homebrew_launcher ? "true" : "false");
    return ensure_copy(path, (unsigned char *)data, (size_t)n, 0600) < 0 ? -1 : 0;
}

/* GoldHEN's LaunchPad scans /data/payloads for standalone ELF files. */
static bool goldhen_installed(void) {
    Path p;
    at(p, GOLDHEN_DIR);
    if (is_dir(p))
        return true;
    at(p, PAYLOADS_DIR);
    return is_dir(p);
}
static int list_in_goldhen(const unsigned char *elf, size_t n) {
    Path p;
    at(p, PAYLOADS_DIR);
    if (ensure_dir(p))
        return -1;
    at(p, PAYLOADS_COPY);
    return ensure_copy(p, elf, n, 0755) < 0 ? -1 : 0;
}
static int list_in_homebrew_launcher(const unsigned char *elf, size_t n) {
    Path p;
    at(p, HOMEBREW_APP);
    if (ensure_dir(p))
        return -1;
    at(p, HOMEBREW_APP "/sce_sys");
    if (ensure_dir(p))
        return -1;
    at(p, HOMEBREW_APP "/eboot.elf");
    if (ensure_copy(p, elf, n, 0755) < 0)
        return -1;
    at(p, HOMEBREW_APP "/sce_sys/icon0.png");
    return ensure_copy(p, launcher_icon, sizeof launcher_icon, 0644) < 0 ? -1 : 0;
}
/* Keeps every copy Orbit placed for a payload manager current: the starter, or the saved
 * runtime in builds without one. */
static int refresh_copies(const unsigned char *service, size_t service_length) {
    const unsigned char *elf;
    size_t n;
    manager_copy(service, service_length, &elf, &n);
    Integrations settings;
    if (integrations_read(&settings)) {
        puts("Orbit: manager preferences could not be read; external copies are unchanged.");
        return -1;
    }
    int failed = 0;
    if (settings.payload_manager && goldhen_installed() && list_in_goldhen(elf, n)) {
        failed = 1;
        printf("Orbit: could not list Orbit in GoldHEN's payloads.\n");
    }
    Path p;
    at(p, HOMEBREW_DIR);
    if (settings.homebrew_launcher && is_dir(p) && list_in_homebrew_launcher(elf, n)) {
        failed = 1;
        printf("Orbit: could not list Orbit in Homebrew Launcher.\n");
    }
    return failed ? -1 : 0;
}

void autoboot_saved_version(char *version, size_t cap) {
    Path p;
    unsigned char *data;
    size_t n;
    version[0] = 0;
    at(p, ORBIT_DIR "/saved-version.txt");
    if (!read_regular_file(p, 63, &data, &n)) {
        copy_text(version, cap, (char *)data);
        free(data);
    }
}

int autoboot_install(const unsigned char *image, size_t length, const char *version) {
    if (!orbit.autoboot)
        return 0;
    int lock = lock_settings();
    if (lock < 0) {
        puts("Orbit auto-start: start-up settings cannot be locked.");
        return -1;
    }
    int updated = 0;
    if (image) {
        Path p;
        at(p, SAVED);
        updated = ensure_copy(p, image, length, 0755);
        if (updated < 0)
            puts("Orbit auto-start: cannot save " SAVED ".");
    }
    if (updated >= 0 && image && version) {
        Path p;
        at(p, ORBIT_DIR "/saved-version.txt");
        if (ensure_copy(p, (const unsigned char *)version, strlen(version), 0600) < 0)
            updated = -1;
    }
    if (save_elfldr()) {
        puts("Orbit auto-start: cannot save " SAVED_ELFLDR ".");
        updated = -1;
    }
    unsigned char *elf;
    size_t n;
    char saved_version[64];
    autoboot_saved_version(saved_version, sizeof saved_version);
    if (!saved_image(&elf, &n)) {
        if (refresh_copies(elf, n))
            updated = -1;
        free(elf);
    } else {
        updated = -1;
    }
    close(lock);
    return updated;
}

/* Startup: saves this payload's runtime, unless the saved copy is a newer release. The TV app
 * may start the copy in its package after Orbit's updater has saved a newer one. */
int autoboot_sync(const unsigned char *image, size_t length) {
    char saved[64];
    autoboot_saved_version(saved, sizeof saved);
    if (image && *saved && orbit_version_compare(saved, ORBIT_VERSION) > 0) {
        printf("Orbit: keeping the newer saved copy (%s); this session runs %s.\n", saved,
               ORBIT_VERSION);
        image = NULL;
    }
    return autoboot_install(image, length, image ? ORBIT_VERSION : NULL);
}

cJSON *autoboot_status(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "available", orbit.autoboot);
    if (!orbit.autoboot)
        return o;
    Integrations settings;
    if (integrations_read(&settings))
        cJSON_AddStringToObject(o, "preferencesError", "Manager preferences could not be read. Automatic copy updates are paused.");
    Path p;
    at(p, SAVED);
    cJSON_AddStringToObject(o, "savedPath", SAVED);
    cJSON_AddBoolToObject(o, "saved", is_file(p));
    cJSON *pm = cJSON_AddObjectToObject(o, "payloadManager");
    cJSON_AddBoolToObject(pm, "managed", settings.payload_manager);
    cJSON_AddBoolToObject(pm, "installed", goldhen_installed());
    at(p, PAYLOADS_COPY);
    cJSON_AddBoolToObject(pm, "listed", is_file(p));
    cJSON *hb = cJSON_AddObjectToObject(o, "homebrewLauncher");
    cJSON_AddBoolToObject(hb, "managed", settings.homebrew_launcher);
    at(p, HOMEBREW_DIR);
    cJSON_AddBoolToObject(hb, "installed", is_dir(p));
    at(p, HOMEBREW_APP "/eboot.elf");
    cJSON_AddBoolToObject(hb, "listed", is_file(p));
    at(p, GOLDHEN_DIR);
    cJSON *gh = cJSON_AddObjectToObject(o, "goldHEN");
    cJSON_AddBoolToObject(gh, "installed", is_dir(p));
    at(p, AUTORUN);
    unsigned char *ini = NULL; size_t length = 0;
    int rc = read_regular_file(p, 1024 * 1024, &ini, &length);
    cJSON_AddBoolToObject(gh, "enabled", !rc && goldhen_autorun_enabled((char *)ini, length));
    cJSON_AddBoolToObject(gh, "readable", !rc || errno == ENOENT);
    free(ini);
    return o;
}

int integration_set(const char *manager, bool enable, char *err, size_t cap) {
    if (!orbit.autoboot) {
        copy_text(err, cap, "Manager integration is set up from Orbit on your PS4.");
        return 409;
    }
    bool pm = !strcmp(manager, "payload-manager");
    if (!pm && strcmp(manager, "homebrew-launcher")) {
        copy_text(err, cap, "Unknown payload manager.");
        return 404;
    }
    int lock = lock_settings();
    if (lock < 0) {
        copy_text(err, cap, "Orbit's manager settings are busy. Try again.");
        return 503;
    }
    int code = 200;
    Integrations settings;
    Path path;
    unsigned char *elf = NULL; size_t length = 0;
    at(path, HOMEBREW_DIR);
    if (integrations_read(&settings)) {
        copy_text(err, cap, "Manager preferences could not be read. No manager files were changed.");
        code = 503;
    } else if (enable && (pm ? !goldhen_installed() : !is_dir(path))) {
        copy_text(err, cap, "This payload manager is not installed.");
        code = 409;
    } else if (enable && saved_image(&elf, &length)) {
        copy_text(err, cap, "Orbit's saved copy is missing. Run orbit_store.elf again.");
        code = 409;
    } else {
        if (pm) settings.payload_manager = enable;
        else settings.homebrew_launcher = enable;
        if (integrations_save(&settings)) {
            copy_text(err, cap, "Could not save your choice. No manager files were changed.");
            code = 503;
        } else if (enable) {
            const unsigned char *copy;
            size_t copy_length;
            manager_copy(elf, length, &copy, &copy_length);
            if (save_elfldr() ||
                (pm ? list_in_goldhen(copy, copy_length) : list_in_homebrew_launcher(copy, copy_length))) {
                copy_text(err, cap, "Your choice was saved, but Orbit could not finish adding its copy. Retry setup.");
                code = 503;
            }
        }
    }
    free(elf);
    close(lock);
    return code;
}

static int list_manager_copy(const unsigned char *service, size_t service_length) {
    const unsigned char *copy;
    size_t copy_length;
    manager_copy(service, service_length, &copy, &copy_length);
    return list_in_goldhen(copy, copy_length);
}

int autoboot_set(const char *manager, bool enable, char *err, size_t cap) {
    if (!orbit.autoboot) {
        copy_text(err, cap, "Auto-start is set up from Orbit on your PS4.");
        return 409;
    }
    if (strcmp(manager, "payload-manager")) {
        copy_text(err, cap, "Unknown payload manager.");
        return 404;
    }
    int lock = lock_settings();
    if (lock < 0) {
        copy_text(err, cap, "Orbit's manager settings are busy. Try again.");
        return 503;
    }
    Path path; at(path, GOLDHEN_DIR);
    Integrations settings;
    unsigned char *ini = NULL, *elf = NULL;
    char *updated = NULL;
    size_t length = 0, updated_length = 0, elf_length = 0;
    int code = 200;
    if (!is_dir(path)) {
        copy_text(err, cap, "This payload manager is not installed."); code = 409;
    } else if (integrations_read(&settings)) {
        copy_text(err, cap, "Manager preferences could not be read. No manager files were changed."); code = 503;
    } else if (enable && !settings.payload_manager) {
        copy_text(err, cap, "Add Orbit under Payload managers first, then choose whether it should start automatically."); code = 409;
    } else if (enable && (saved_image(&elf, &elf_length) || save_elfldr() || list_manager_copy(elf, elf_length))) {
        copy_text(err, cap, "Orbit's saved copy is missing. Run orbit_store.elf again."); code = 409;
    } else {
        at(path, AUTORUN);
        int rc = read_regular_file(path, 1024 * 1024, &ini, &length);
        if ((rc && errno != ENOENT) ||
            goldhen_autorun_edit(ini ? (char *)ini : "", length, enable, &updated, &updated_length) ||
            (updated_length && ensure_copy(path, (unsigned char *)updated, updated_length, 0644) < 0)) {
            copy_text(err, cap, "Could not update GoldHEN's AutoRun list. Check that payloads.ini is a writable regular file."); code = 503;
        } else if (!updated_length && !rc && ensure_copy(path, (unsigned char *)"", 0, 0644) < 0) {
            copy_text(err, cap, "Could not update GoldHEN's AutoRun list. Check that payloads.ini is a writable regular file."); code = 503;
        }
    }
    free(elf); free(ini); free(updated);
    close(lock);
    return code;
}
