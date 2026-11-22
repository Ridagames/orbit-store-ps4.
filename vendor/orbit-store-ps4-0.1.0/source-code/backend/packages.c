/* Download receipts, package installation and ShadowMount registration.
 * No caller-supplied absolute path reaches AppInstUtil. Files are opened below
 * the original drive with O_NOFOLLOW, then revalidated before submission.
 * A receipt's files are in the drive's homebrew folder, or (from Files) in
 * the folder its "folder" names, from the drive's top.
 * Installation is explicit. Scanning a newly finished image is automatic.
 * With deletePackages on, a package is deleted only after the PS4 reports it
 * installed, and only if it is still the file that was checked. */
#include "packages.h"
#include "power.h"
#include "files.h"
#include "extract.h"
#include <dirent.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PACKAGE_LIMIT 64
#define FILE_LIMIT 100000
#define DB_LIMIT (8U * 1024U * 1024U)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static bool started, stopping, dirty, broken;
static cJSON *receipts;
static void text(cJSON *o, const char *key, const char *value) {
    cJSON_DeleteItemFromObjectCaseSensitive(o, key); cJSON_AddStringToObject(o, key, value);
}
static void number(cJSON *o, const char *key, double value) {
    cJSON_DeleteItemFromObjectCaseSensitive(o, key); cJSON_AddNumberToObject(o, key, value);
}
static void identity(cJSON *o, const char *key, uint64_t n) {
    char s[32]; snprintf(s, sizeof s, "%" PRIu64, n); text(o, key, s);
}
static uint64_t ident(const cJSON *o, const char *key) { return strtoull(json_text(o, key), NULL, 10); }
static bool yes(const cJSON *o, const char *key) { return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key)); }
static cJSON *find(const char *id) {
    cJSON *r; cJSON_ArrayForEach(r, receipts) if (!strcmp(json_text(r, "id"), id)) return r;
    return NULL;
}
static cJSON *find_origin(const char *origin) {
    cJSON *r; cJSON_ArrayForEach(r, receipts) if (*origin && !strcmp(json_text(r, "origin"), origin)) return r;
    return NULL;
}
static cJSON *find_package(cJSON *r, const char *id) {
    cJSON *p; cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages"))
        if (!strcmp(json_text(p, "id"), id)) return p;
    return NULL;
}
static bool in_flight(const cJSON *p) {
    const char *s = json_text(p, "state");
    return !strcmp(s, "queued") || !strcmp(s, "installing") || !strcmp(s, "unconfirmed");
}
static void hash_id(const char *input, char out[17]) {
    unsigned char hash[32]; SHA256((const unsigned char *)input, strlen(input), hash);
    for (int i = 0; i < 8; i++) snprintf(out + 2 * i, 3, "%02x", hash[i]);
}
static bool relative(const char *path) {
    if (!*path || *path == '/' || strlen(path) >= 768) return false;
    char copy[768]; copy_text(copy, sizeof copy, path);
    char *save = NULL;
    for (char *p = strtok_r(copy, "/", &save); p; p = strtok_r(NULL, "/", &save))
        if (!extract_name_ok(p) || p[0] == '.') return false;
    return !strstr(path, "//") && path[strlen(path) - 1] != '/';
}
/* The receipt's folder from the drive's top: homebrew unless Files chose another
 * ("." is the top itself). */
static const char *folder_of(const cJSON *r) {
    const char *folder = json_text(r, "folder");
    return !*folder ? "homebrew" : !strcmp(folder, ".") ? "" : folder;
}
/* root/folder/name, without an empty folder. */
static void receipt_path(const cJSON *r, const char *name, char *out, size_t cap) {
    const char *folder = folder_of(r);
    snprintf(out, cap, "%s%s%s/%s", json_text(r, "root"), *folder ? "/" : "", folder, name);
}
static void pin_from(const cJSON *r, Job *pin) {
    memset(pin, 0, sizeof *pin);
    copy_text(pin->root, sizeof pin->root, json_text(r, "root"));
    copy_text(pin->storage_id, sizeof pin->storage_id, json_text(r, "storageId"));
    pin->device = (dev_t)ident(r, "device"); pin->inode = (ino_t)ident(r, "inode");
}
static int open_file(const cJSON *r, const char *name, struct stat *st) {
    if (!relative(name)) { errno = EINVAL; return -1; }
    Job pin; pin_from(r, &pin);
    int parent = storage_open_path(&pin, folder_of(r));
    if (parent < 0) return -1;
    char copy[768]; copy_text(copy, sizeof copy, name);
    char *save = NULL, *part = strtok_r(copy, "/", &save);
    while (part) {
        char *next = strtok_r(NULL, "/", &save);
        int fd = openat(parent, part, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | (next ? O_DIRECTORY : 0));
        close(parent);
        if (fd < 0) return -1;
        struct stat value;
        if (fstat(fd, &value) || value.st_dev != pin.device ||
            (next ? !S_ISDIR(value.st_mode) : !S_ISREG(value.st_mode) || value.st_nlink != 1)) {
            close(fd); errno = EINVAL; return -1;
        }
        parent = fd; if (!next) { *st = value; return fd; }
        part = next;
    }
    close(parent); return -1;
}
/* The directory holding a receipt's file, opened as open_file() walks to it. */
static int open_parent(const cJSON *r, const char *name, char leaf[256]) {
    if (!relative(name)) { errno = EINVAL; return -1; }
    Job pin; pin_from(r, &pin);
    int parent = storage_open_path(&pin, folder_of(r));
    if (parent < 0) return -1;
    char copy[768]; copy_text(copy, sizeof copy, name);
    char *save = NULL, *part = strtok_r(copy, "/", &save);
    while (part) {
        char *next = strtok_r(NULL, "/", &save);
        if (!next) { copy_text(leaf, 256, part); return parent; }
        int fd = openat(parent, part, O_RDONLY | O_NOFOLLOW | O_DIRECTORY);
        close(parent);
        struct stat value;
        if (fd < 0) return -1;
        if (fstat(fd, &value) || value.st_dev != pin.device || !S_ISDIR(value.st_mode)) { close(fd); errno = EINVAL; return -1; }
        parent = fd; part = next;
    }
    close(parent); errno = EINVAL; return -1;
}
static bool stamp_matches(const cJSON *p, const struct stat *st) {
    return ident(p, "device") == (uint64_t)st->st_dev && ident(p, "inode") == (uint64_t)st->st_ino &&
        ident(p, "mtime") == (uint64_t)st->st_mtime && ident(p, "mtimeNs") == (uint64_t)st->st_mtim.tv_nsec &&
        json_int(p, "sizeBytes") == st->st_size;
}
static int persist(void) {
    if (broken) return -1;
    char *data = cJSON_PrintUnformatted(receipts);
    if (!data) return -1;
    size_t len = strlen(data), offset = 0;
    int fd = len > DB_LIMIT ? -1 : openat(orbit.state_fd, "installations.tmp", O_WRONLY | O_CREAT | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        struct stat st;
        if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 && !ftruncate(fd, 0)) {
            while (offset < len) {
                ssize_t n = write(fd, data + offset, len - offset);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                offset += (size_t)n;
            }
        }
    }
    int result = fd >= 0 && offset == len && !fsync(fd) ? 0 : -1;
    if (fd >= 0 && close(fd)) result = -1;
    free(data);
    if (!result && renameat(orbit.state_fd, "installations.tmp", orbit.state_fd, "installations.json")) result = -1;
    if (!result && fsync(orbit.state_fd)) result = -1;
    if (!result) dirty = false;
    return result;
}
static void load(void) {
    receipts = cJSON_CreateArray();
    int fd = openat(orbit.state_fd, "installations.json", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) { if (errno != ENOENT) broken = true; return; }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > DB_LIMIT) { close(fd); broken = true; return; }
    char *data = calloc((size_t)st.st_size + 1, 1); size_t got = 0;
    if (data) while (got < (size_t)st.st_size) {
        ssize_t n = read(fd, data + got, (size_t)st.st_size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    cJSON *parsed = data && got == (size_t)st.st_size ? cJSON_ParseWithLengthOpts(data, got + 1, NULL, true) : NULL;
    free(data);
    /* The on-disk byte limit bounds this history, not a lifetime install count.
     * Existing receiver/stream receipts must survive even beyond 512 records. */
    if (!cJSON_IsArray(parsed)) { cJSON_Delete(parsed); broken = true; return; }
    cJSON *r, *p;
    cJSON_ArrayForEach(r, parsed) {
        /* A process-local removal guard must not survive an interrupted deletion. */
        cJSON_DeleteItemFromObjectCaseSensitive(r, "removingFiles");
        if (strlen(json_text(r, "id")) != 16 || !relative(json_text(r, "output")) || strchr(json_text(r, "output"), '/') ||
            (*json_text(r, "folder") && strcmp(json_text(r, "folder"), ".") && !relative(json_text(r, "folder"))) ||
            !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(r, "packages")) || cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r, "packages")) > PACKAGE_LIMIT) { broken = true; break; }
        cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
            if (!relative(json_text(p, "filename")) || strlen(json_text(p, "id")) != 12) { broken = true; break; }
            /* Never resubmit a request after a crash: it may have reached PS4. */
            if (!strcmp(json_text(p, "state"), "installing") || !strcmp(json_text(p, "state"), "queued")) {
                text(p, "state", "unconfirmed");
                text(p, "message", "Orbit restarted. Check installation status before trying again.");
            }
        }
    }
    if (broken) cJSON_Delete(parsed);
    else { cJSON_Delete(receipts); receipts = parsed; }
}
/* Only settled records may be forgotten. Unknown states fail closed, including
 * interrupted installations and ShadowMount requests awaiting confirmation. */
static bool settled_receipt(const cJSON *r) {
    const char *state = json_text(r, "state"), *mount = json_text(r, "shadowMount");
    if ((strcmp(state, "ready") && strcmp(state, "error")) || yes(r, "removingFiles") || yes(r, "autoInstall")) return false;
    if (strcmp(mount, "none") && strcmp(mount, "available") && strcmp(mount, "registered") && strcmp(mount, "unavailable")) return false;
    const cJSON *p;
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
        state = json_text(p, "state");
        if (strcmp(state, "ready") && strcmp(state, "installed") && strcmp(state, "error") && strcmp(state, "blocked")) return false;
    }
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "mountTargets")) {
        state = json_text(p, "state");
        if (strcmp(state, "registered") && strcmp(state, "available") && strcmp(state, "unavailable")) return false;
    }
    return true;
}
/* Store queue IDs are 16 hex characters. Receiver/stream IDs are 32; Files
 * origins have a files- prefix. Never guess whether Zero still needs a receipt.
 * Hold queue -> package locks, as queue serialization already does, so a live
 * download cannot disappear between checking its identity and pruning history.
 * No drive is opened and no game file or installation is changed here. */
static void prune_download_history(void) {
    pthread_mutex_lock(&orbit.mutex);
    pthread_mutex_lock(&lock);
    cJSON *before = NULL;
    if (receipts && !broken && !stopping) {
        cJSON *r = receipts->child;
        while (r) {
            cJSON *next = r->next;
            const char *origin = json_text(r, "origin");
            if (!yes(r, "stream") && strlen(origin) == 16 && strspn(origin, "0123456789abcdef") == 16 &&
                !job_find(origin) && settled_receipt(r)) {
                if (!before && !(before = cJSON_Duplicate(receipts, true))) break;
                cJSON_Delete(cJSON_DetachItemViaPointer(receipts, r));
                dirty = true;
            }
            r = next;
        }
        if (before && persist()) {
            cJSON_Delete(receipts); receipts = before; before = NULL;
            /* A rename may have succeeded before directory fsync failed. */
            dirty = true;
        }
    }
    cJSON_Delete(before);
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&orbit.mutex);
}
void packages_completed(const Job *pin, const char *origin, const char *output, bool scan) {
    if (!output || !extract_name_ok(output) || output[0] == '.') return;
    pthread_mutex_lock(&lock);
    if (!receipts || stopping || broken || find_origin(origin)) { pthread_mutex_unlock(&lock); return; }
    char data[1100], id[17];
    snprintf(data, sizeof data, "%s\n%s\n%s", pin->root, output, origin); hash_id(data, id);
    if (find(id)) { pthread_mutex_unlock(&lock); return; }
    cJSON *r = cJSON_CreateObject();
    text(r, "id", id); text(r, "origin", origin); text(r, "root", pin->root);
    text(r, "storageId", pin->storage_id); text(r, "output", output);
    char display[512]; release_download_title(&pin->release, display, sizeof display);
    text(r, "title", display[0] ? display : output);
    if (pin->release.addon_id[0]) {
        text(r, "addonId", pin->release.addon_id); text(r, "baseGameId", pin->release.base_game_id);
        text(r, "addonType", pin->release.addon_type);
        text(r, "baseGameTitle", pin->release.base_title);
        text(r, "contentId", pin->release.content_id);
        cJSON_AddItemToObject(r, "requirements", cJSON_Parse(pin->release.requirements));
    }
    if (pin->release.update_version[0]) {
        text(r, "updateId", pin->release.game_id); text(r, "updateVersion", pin->release.update_version);
        text(r, "baseGameId", pin->release.base_game_id); text(r, "baseGameTitle", pin->release.base_title);
    }
    /* Get everything and Update: install once checked, after install_after's packages. */
    if (pin->auto_install) {
        cJSON_AddBoolToObject(r, "autoInstall", true);
        if (pin->install_after[0]) text(r, "installAfter", pin->install_after);
    }
    identity(r, "device", pin->device); identity(r, "inode", pin->inode);
    text(r, "state", "checking"); text(r, "message", "Checking downloaded files…");
    cJSON_AddArrayToObject(r, "packages");
    cJSON_AddBoolToObject(r, "autoScan", scan);
    text(r, "shadowMount", "none");
    cJSON_AddItemToArray(receipts, r); dirty = true;
    if (persist()) {
        cJSON_Delete(cJSON_DetachItemViaPointer(receipts, r));
        dirty = true;
    } else pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}
static void install_checked(cJSON *r);
/* Get everything: a download its page added can finish before the list marks it; mark
 * its receipt too, unless something already happened to its packages. Queue -> package
 * lock order, as advance_chains(). */
void packages_auto_install(const char *origin, const char *after) {
    pthread_mutex_lock(&lock);
    cJSON *r = receipts && !broken && !stopping ? find_origin(origin) : NULL, *p;
    const char *state = r ? json_text(r, "state") : "";
    bool untouched = r && !yes(r, "autoInstall") && (!strcmp(state, "checking") || !strcmp(state, "ready"));
    if (untouched) cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages"))
        if (strcmp(json_text(p, "state"), "ready")) untouched = false;
    if (untouched) {
        cJSON_AddBoolToObject(r, "autoInstall", true);
        if (*after) text(r, "installAfter", after);
        if (!strcmp(state, "ready")) install_checked(r);
        dirty = true;
        pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&lock);
}
static bool extension(const char *name, const char *suffix) {
    size_t a = strlen(name), b = strlen(suffix); return a > b && !strcasecmp(name + a - b, suffix);
}
static bool content_id_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 21 || n > 36 || strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != n) return false;
    return s[6] == '-' && s[16] == '_' && s[19] == '-';
}
static int examine(int fd, cJSON *p, struct stat *st) {
    unsigned char header[0x440];
    if (pread(fd, header, sizeof header, 0) != sizeof header) return -1;
    bool ps5 = !memcmp(header, "\x7f" "FIH", 4);
    if (!ps5 && memcmp(header, "\x7f" "CNT", 4)) return -1;
    char cid[37] = {0};
    if (!ps5) {
        memcpy(cid, header + 0x40, 36);
        if (!content_id_ok(cid)) return -1;
        uint64_t declared = 0; for (int i = 0; i < 8; i++) declared = (declared << 8) | header[0x430 + i];
        if (declared && declared != (uint64_t)st->st_size) return -1;
        if (!strncmp(cid + 7, "PPSA", 4)) ps5 = true;
    }
    text(p, "contentId", cid); text(p, "platform", ps5 ? "ps5" : "ps4");
    /* Identified but not installable here: the console installer rejects PS5
     * packages and a missing content id gives it nothing to register. */
    return ps5 || !*cid ? 2 : 0;
}
static int walk(int parent, const char *name, const char *prefix, cJSON *r, unsigned depth, unsigned *visited, bool *mountable) {
    if (++*visited > FILE_LIMIT || depth > 16 || !extract_name_ok(name)) return -1;
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) || st.st_dev != (dev_t)ident(r, "device")) return -1;
    char relative_path[768];
    if (snprintf(relative_path, sizeof relative_path, "%s%s%s", prefix, *prefix ? "/" : "", name) >= (int)sizeof relative_path) return -1;
    if (S_ISDIR(st.st_mode)) {
        int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd < 0) return -1;
        struct stat actual;
        if (fstat(fd, &actual) || st.st_ino != actual.st_ino || st.st_dev != actual.st_dev) { close(fd); return -1; }
        DIR *dir = fdopendir(fd); if (!dir) { close(fd); return -1; }
        int result = 0; struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (walk(fd, entry->d_name, relative_path, r, depth + 1, visited, mountable)) { result = -1; break; }
        }
        closedir(dir); return result;
    }
    if (!S_ISREG(st.st_mode) || st.st_nlink != 1) return -1;
    if (extension(name, ".exfat") || extension(name, ".ffpfsc") || extension(name, ".ffpkg") || !strcasecmp(name, "eboot.bin")) *mountable = true;
    if (!extension(name, ".pkg") && !extension(name, ".fpkg")) return 0;
    cJSON *list = cJSON_GetObjectItemCaseSensitive(r, "packages");
    if (cJSON_GetArraySize(list) >= PACKAGE_LIMIT) return -1;
    cJSON *p = cJSON_CreateObject(); char hash[17], stamp[1000];
    snprintf(stamp, sizeof stamp, "%s:%ju:%ju:%jd:%jd:%ld", relative_path, (uintmax_t)st.st_dev, (uintmax_t)st.st_ino,
             (intmax_t)st.st_size, (intmax_t)st.st_mtime, st.st_mtim.tv_nsec);
    hash_id(stamp, hash); hash[12] = 0;
    text(p, "id", hash); text(p, "filename", relative_path); text(p, "state", "ready"); text(p, "message", "Ready to install on PS4.");
    number(p, "sizeBytes", (double)st.st_size); identity(p, "device", st.st_dev); identity(p, "inode", st.st_ino);
    identity(p, "mtime", st.st_mtime); identity(p, "mtimeNs", st.st_mtim.tv_nsec);
    int fd = openat(parent, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat actual;
    int erc = -1;
    if (fd >= 0 && !fstat(fd, &actual) && stamp_matches(p, &actual)) erc = examine(fd, p, &actual);
    if (erc) {
        text(p, "state", "blocked");
        text(p, "message", erc == 2
            ? "This is a PS5 package; it can't be installed on PS4. Try another download option."
            : "This file is not a complete supported package. Try another download option.");
    }
    if (fd >= 0) close(fd);
    cJSON_AddItemToArray(list, p); return 0;
}
static void inspect(cJSON *r) {
    Job pin; pin_from(r, &pin);
    if (!pin.device && !pin.inode) {
        struct stat st;
        if (!lstat(pin.root, &st) && S_ISDIR(st.st_mode)) {
            pin.device = st.st_dev; pin.inode = st.st_ino;
            identity(r, "device", st.st_dev); identity(r, "inode", st.st_ino);
        }
    }
    int root = storage_open_path(&pin, folder_of(r));
    unsigned visited = 0; bool mountable = false;
    int code = root < 0 ? -1 : walk(root, json_text(r, "output"), "", r, 0, &visited, &mountable);
    if (root >= 0) close(root);
    text(r, "state", code ? "error" : "ready");
    text(r, "message", code ? "Could not check the downloaded files. Reconnect the original drive and try again." : "");
    if (code) { cJSON_DeleteItemFromObjectCaseSensitive(r, "packages"); cJSON_AddArrayToObject(r, "packages"); }
    else if (mountable) text(r, "shadowMount", yes(r, "autoScan") ? "pending" : "available");
}
static cJSON *public_record(const cJSON *r) {
    if (!r) return cJSON_CreateNull();
    cJSON *out = cJSON_CreateObject();
    const char *keys[] = {"id", "origin", "state", "message", "shadowMount", "shadowMountMessage", "storageId", "output", "title", "addonId", "baseGameId", "baseGameTitle", "addonType", "contentId", "delivery",
                          "updateId", "updateVersion", "installAfter", "chain", "chainTitle"};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) text(out, keys[i], json_text(r, keys[i]));
    cJSON_AddBoolToObject(out, "autoInstall", yes(r, "autoInstall"));
    const cJSON *requirements = cJSON_GetObjectItemCaseSensitive(r, "requirements");
    if (requirements) cJSON_AddItemToObject(out, "requirements", cJSON_Duplicate(requirements, true));
    char path[1900]; receipt_path(r, json_text(r, "output"), path, sizeof path); text(out, "path", yes(r, "stream") ? "" : path);
    cJSON_AddBoolToObject(out, "available", started && !broken && package_platform_available());
    cJSON *list = cJSON_AddArrayToObject(out, "packages"), *p;
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
        cJSON *item = cJSON_CreateObject();
        const char *names[] = {"id", "filename", "state", "message", "platform", "contentId"};
        for (size_t i = 0; i < sizeof names / sizeof *names; i++) text(item, names[i], json_text(p, names[i]));
        number(item, "received", (double)json_int(p, "received")); number(item, "total", (double)json_int(p, "total"));
        cJSON_AddItemToArray(list, item);
    }
    return out;
}
cJSON *packages_for_download(const char *origin) {
    pthread_mutex_lock(&lock); cJSON *out = public_record(find_origin(origin)); pthread_mutex_unlock(&lock); return out;
}
cJSON *packages_status(void) {
    pthread_mutex_lock(&lock); cJSON *out = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(out, "downloads"), *r;
    cJSON_AddBoolToObject(out, "available", started && !broken && package_platform_available());
    text(out, "message", broken ? "Installation history could not be read. Your downloads are safe; check Orbit's saved data before installing." : "");
    cJSON_ArrayForEach(r, receipts) cJSON_AddItemToArray(list, public_record(r));
    pthread_mutex_unlock(&lock); return out;
}
static bool any_active(void) {
    cJSON *r, *p; cJSON_ArrayForEach(r, receipts) cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages"))
        if (!strcmp(json_text(p, "state"), "queued") || !strcmp(json_text(p, "state"), "installing")) return true;
    return false;
}
/* Zero's URL is constructed from the authenticated caller's IPv4 address.
 * No arbitrary URL, hostname, filesystem path or redirect enters AppInstUtil. */
int packages_stream(const cJSON *input, const char *peer, char *error, size_t cap) {
    const char *origin = json_text(input, "streamId"), *token = json_text(input, "token"), *name = json_text(input, "filename");
    struct in_addr address; unsigned a = 0, b = 0; double port_value = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(input, "port"));
    int port = port_value >= 1024 && port_value <= 65535 ? (int)port_value : 0;
    double size = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(input, "sizeBytes"));
    sscanf(peer, "%u.%u", &a, &b);
    bool local = a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (orbit.desktop && a == 127);
    if (!local || inet_pton(AF_INET, peer, &address) != 1 || strlen(origin) != 32 || strspn(origin, "0123456789abcdef") != 32 ||
        strlen(token) != 64 || strspn(token, "0123456789abcdef") != 64 || port < 1024 || port > 65535 ||
        cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(input, "port")) != port ||
        !(size >= 0x440 && size <= 9007199254740991.0) || (double)(uint64_t)size != size ||
        !extract_name_ok(name) || name[0] == '.' || strlen(name) > 240 ||
        !(extension(name, ".pkg") || extension(name, ".fpkg") || extension(name, ".ffpkg"))) {
        copy_text(error, cap, "Choose a completed package in Orbit Zero."); return 400;
    }
    char url[160], id[17]; snprintf(url, sizeof url, "http://%s:%d/orbit-package/%s.pkg", peer, port, token);
    hash_id(origin, id);
    pthread_mutex_lock(&lock);
    int code = 202; cJSON *old = find_origin(origin);
    if (!started || stopping || broken || !package_platform_available()) {
        code = 503; copy_text(error, cap, "The PS4 package installer is unavailable. Check Orbit and your jailbreak.");
    } else if (old) {
        if (!yes(old, "stream") || strcmp(json_text(old, "streamUrl"), url)) {
            code = 409; copy_text(error, cap, "This installation belongs to a different computer session.");
        }
        /* Repeating an accepted request returns the receipt; never queues it twice. */
    } else if (any_active()) {
        code = 409; copy_text(error, cap, "Finish the current installation or check its status before starting another.");
    } else {
        cJSON *r = cJSON_CreateObject(), *p = cJSON_CreateObject();
        text(r, "id", id); text(r, "origin", origin); text(r, "output", name); text(r, "title", name);
        text(r, "state", "ready"); text(r, "shadowMount", "none"); text(r, "delivery", "stream");
        cJSON_AddBoolToObject(r, "stream", true); text(r, "streamUrl", url);
        text(p, "id", "000000000001"); text(p, "filename", name); number(p, "sizeBytes", size);
        text(p, "state", "queued"); text(p, "message", "Waiting to install from your computer…");
        cJSON_AddItemToArray(cJSON_AddArrayToObject(r, "packages"), p); cJSON_AddItemToArray(receipts, r);
        dirty = true;
        if (persist()) { cJSON_Delete(cJSON_DetachItemViaPointer(receipts, r)); code = 503;
            copy_text(error, cap, "Could not save installation progress. No installation was started."); }
        else pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&lock); return code;
}
int packages_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    if (!strcmp(action, "review")) {
        const char *storage = json_text(input, "storageId"), *name = json_text(input, "filename"), *origin = json_text(input, "origin");
        if (!extract_name_ok(name) || name[0] == '.' || strlen(origin) != 32 || strspn(origin, "0123456789abcdef") != 32) {
            copy_text(error, cap, "Choose a completed PS4 transfer to check."); return 400;
        }
        Job pin = {0}; copy_text(pin.storage_id, sizeof pin.storage_id, storage);
        if (orbit.desktop && !strcmp(storage, "desktop")) copy_text(pin.root, sizeof pin.root, orbit.desktop_storage);
        else if (!orbit.desktop && !strcmp(storage, "internal")) copy_text(pin.root, sizeof pin.root, "/data");
        else if (!orbit.desktop && strlen(storage) == 4 && storage[3] >= '0' && storage[3] <= '7' &&
            (!strncmp(storage, "usb", 3) || !strncmp(storage, "ext", 3))) snprintf(pin.root, sizeof pin.root, "/mnt/%s", storage);
        else { copy_text(error, cap, "Choose a supported PS4 drive."); return 400; }
        packages_completed(&pin, origin, name, false);
        pthread_mutex_lock(&lock); bool created = find_origin(origin) != NULL; pthread_mutex_unlock(&lock);
        if (!created) { copy_text(error, cap, "Could not record this completed transfer. Check Orbit's saved data."); return 503; }
        return 202;
    }
    pthread_mutex_lock(&lock); int code = 202;
    cJSON *r = find(json_text(input, "downloadId")), *p = find_package(r, json_text(input, "packageId"));
    cJSON *before = r ? cJSON_Duplicate(r, true) : NULL;
    if (r && !before) { code = 503; copy_text(error, cap, "Could not save installation progress. No installation was started."); }
    else if (!started || stopping || broken) { code = 503; copy_text(error, cap, "The package installer is unavailable. Your downloaded files are safe."); }
    else if (yes(r, "removingFiles")) { code = 409; copy_text(error, cap, "Wait for file removal to finish."); }
    else if (!r) { code = 404; copy_text(error, cap, "This completed download has not been checked yet."); }
    else if (yes(r, "stream") && strcmp(action, "check")) { code = 409; copy_text(error, cap, "Manage this streaming installation in Orbit Zero."); }
    else if (!strcmp(action, "scan")) {
        if (!strcmp(json_text(r, "shadowMount"), "none")) { code = 409; copy_text(error, cap, "No game image or folder to register was found in this download."); }
        else { text(r, "shadowMount", "pending"); text(r, "shadowMountMessage", "Waiting to check the library…"); number(r, "scanChecks", 0); number(r, "scanAfter", 0); }
    } else if (!strcmp(action, "inspect")) {
        bool active = false; cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(r, "packages")) if (in_flight(item)) active = true;
        if (active) { code = 409; copy_text(error, cap, "Check the current installation before checking this download again."); }
        else { text(r, "state", "checking"); cJSON_DeleteItemFromObjectCaseSensitive(r, "packages"); cJSON_AddArrayToObject(r, "packages"); }
    } else if (!p) { code = 404; copy_text(error, cap, "Choose a package from this completed download."); }
    else if (!strcmp(action, "check")) {
        if (!*json_text(p, "installTask")) { code = 409; copy_text(error, cap, "Check the PS4 home screen. The console did not provide an installation ID; Orbit will not submit it twice."); }
        else if (strcmp(json_text(p, "state"), "unconfirmed")) { code = 409; copy_text(error, cap, "This installation is not awaiting a status check."); }
        else { text(p, "state", "installing"); number(p, "pollErrors", 0); }
    } else if (strcmp(action, "install")) { code = 400; copy_text(error, cap, "Unknown installation action."); }
    else if (!package_platform_available()) { code = 409; copy_text(error, cap, "Package installation is available on a PS4 running the matching Orbit ELF."); }
    else if (any_active()) { code = 409; copy_text(error, cap, "Wait for the current installation, or check its status on the PS4 home screen."); }
    else if (strcmp(json_text(p, "state"), "ready") && strcmp(json_text(p, "state"), "error")) { code = 409; copy_text(error, cap, "This package is not ready to install."); }
    else { text(p, "state", "queued"); text(p, "message", "Waiting to install…"); }
    if (code == 202) {
        dirty = true;
        if (persist()) {
            if (before) { cJSON_ReplaceItemViaPointer(receipts, r, before); before = NULL; }
            dirty = true;
            code = 503; copy_text(error, cap, "Could not save installation progress. Check console storage and try again.");
        } else pthread_cond_signal(&changed);
    }
    cJSON_Delete(before); pthread_mutex_unlock(&lock); return code;
}
/* Only a receiver receipt may name the source files of a completed Zero
 * transfer. The caller supplies an origin, never a filesystem path. */
cJSON *packages_remove_files(const cJSON *input, int *code, char *error, size_t cap) {
    const char *origin = json_text(input, "origin"), *action = json_text(input, "action");
    bool deleting = !strcmp(action, "delete");
    if (strlen(origin) != 32 || strspn(origin, "0123456789abcdef") != 32 ||
        (strcmp(action, "preview") && !deleting)) {
        *code = 400; copy_text(error, cap, "Choose a completed PS4 transfer."); return NULL;
    }
    pthread_mutex_lock(&lock);
    cJSON *r = find_origin(origin), *p;
    bool busy = !r || yes(r, "stream") || yes(r, "removingFiles") || strcmp(folder_of(r), "homebrew") ||
        !strcmp(json_text(r, "state"), "checking");
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) if (in_flight(p)) busy = true;
    if (busy) {
        pthread_mutex_unlock(&lock); *code = 409;
        copy_text(error, cap, "Check the completed PS4 transfer before deleting its downloaded files."); return NULL;
    }
    Job pin; pin_from(r, &pin);
    copy_text(pin.output, sizeof pin.output, json_text(r, "output"));
    copy_text(pin.filename, sizeof pin.filename, pin.output);
    copy_text(pin.status, sizeof pin.status, "complete");
    if (deleting) { cJSON_DeleteItemFromObjectCaseSensitive(r, "removingFiles"); cJSON_AddBoolToObject(r, "removingFiles", true); }
    pthread_mutex_unlock(&lock);
    cJSON *plan = files_download_plan(&pin, code, error, cap);
    int rc = !plan ? -1 : deleting ? files_delete_download(&pin, error, cap) : 0;
    if (deleting) {
        pthread_mutex_lock(&lock);
        r = find_origin(origin);
        if (r) cJSON_DeleteItemFromObjectCaseSensitive(r, "removingFiles");
        pthread_mutex_unlock(&lock);
    }
    if (rc) { cJSON_Delete(plan); *code = 409; return NULL; }
    *code = 200;
    cJSON *out = cJSON_CreateObject(); cJSON_AddItemToObject(out, "files", plan); return out;
}
int packages_download_action(const char *origin, const char *action, char *error, size_t cap) {
    cJSON *input = cJSON_CreateObject();
    pthread_mutex_lock(&lock); cJSON *r = find_origin(origin); text(input, "downloadId", json_text(r, "id")); pthread_mutex_unlock(&lock);
    if (!strncmp(action, "install-", 8)) { text(input, "action", "install"); text(input, "packageId", action + 8); }
    else if (!strncmp(action, "check-", 6) && strcmp(action, "check-packages")) { text(input, "action", "check"); text(input, "packageId", action + 6); }
    else text(input, "action", !strcmp(action, "scan-library") ? "scan" : "inspect");
    int code = packages_action(input, error, cap); cJSON_Delete(input); return code;
}
/* Files' Install on PS4: a receipt for one package, game image or game folder in
 * any folder of a drive. Checked by the worker, then its packages install and
 * an image or folder goes to ShadowMount. The same file again reuses its receipt. */
int packages_install_path(const Job *pin, const char *folder, const char *output, const char *origin,
                          char *error, size_t cap) {
    if ((strcmp(folder, ".") && !relative(folder)) || !extract_name_ok(output) || output[0] == '.') {
        copy_text(error, cap, "Choose a PKG file, game image or game folder."); return 400;
    }
    pthread_mutex_lock(&lock);
    int code = 202;
    cJSON *r = receipts ? find_origin(origin) : NULL, *p, *before = NULL;
    bool added = false;
    if (!receipts || stopping || broken || !started) { code = 503; copy_text(error, cap, "The package installer is unavailable. Your files are safe."); }
    else if (r) {
        cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) if (in_flight(p)) code = 409;
        if (code == 409 || !strcmp(json_text(r, "state"), "checking") || !strcmp(json_text(r, "shadowMount"), "pending") ||
            !strcmp(json_text(r, "shadowMount"), "requested")) {
            code = 409; copy_text(error, cap, "This is already being installed. Check its progress in Files.");
        } else if (!(before = cJSON_Duplicate(r, true))) {
            code = 503; copy_text(error, cap, "Could not save installation progress. No installation was started.");
        } else {
            text(r, "state", "checking"); text(r, "message", "Checking files…");
            cJSON_DeleteItemFromObjectCaseSensitive(r, "packages"); cJSON_AddArrayToObject(r, "packages");
            text(r, "shadowMount", "none"); text(r, "shadowMountMessage", "");
            number(r, "scanChecks", 0); number(r, "scanAfter", 0);
            cJSON_DeleteItemFromObjectCaseSensitive(r, "autoInstall"); cJSON_AddBoolToObject(r, "autoInstall", true);
            dirty = true;
        }
    } else {
        char data[1900], id[17];
        snprintf(data, sizeof data, "%s\n%s/%s\n%s", pin->root, folder, output, origin); hash_id(data, id);
        if (find(id)) { pthread_mutex_unlock(&lock); copy_text(error, cap, "This is already being installed. Check its progress in Files."); return 409; }
        r = cJSON_CreateObject();
        text(r, "id", id); text(r, "origin", origin); text(r, "root", pin->root);
        text(r, "storageId", pin->storage_id); text(r, "folder", folder); text(r, "output", output); text(r, "title", output);
        identity(r, "device", pin->device); identity(r, "inode", pin->inode);
        text(r, "state", "checking"); text(r, "message", "Checking files…");
        cJSON_AddArrayToObject(r, "packages");
        cJSON_AddBoolToObject(r, "autoScan", true); cJSON_AddBoolToObject(r, "autoInstall", true);
        text(r, "shadowMount", "none");
        cJSON_AddItemToArray(receipts, r); dirty = true; added = true;
    }
    if (code == 202) {
        if (persist()) {
            if (before) { cJSON_ReplaceItemViaPointer(receipts, r, before); before = NULL; }
            else if (added) cJSON_Delete(cJSON_DetachItemViaPointer(receipts, r));
            dirty = true;
            code = 503; copy_text(error, cap, "Could not save installation progress. No installation was started.");
        } else pthread_cond_signal(&changed);
    }
    cJSON_Delete(before);
    pthread_mutex_unlock(&lock);
    return code;
}
bool packages_path_busy(const char *path) {
    bool found = false; pthread_mutex_lock(&lock); cJSON *r, *p;
    cJSON_ArrayForEach(r, receipts) cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
        if (!in_flight(p)) continue;
        char target[2400]; receipt_path(r, json_text(p, "filename"), target, sizeof target);
        size_t n = strlen(path);
        if (!strcmp(target, path) || (!strncmp(target, path, n) && target[n] == '/')) found = true;
    }
    pthread_mutex_unlock(&lock); return found;
}
static void package_result(const char *rid, const char *pid, const char *state, const char *message, const char *cid, const PackageProgress *progress) {
    pthread_mutex_lock(&lock); cJSON *p = find_package(find(rid), pid);
    if (p) {
        /* Persist transitions, not every byte counter. The system installer
         * owns its checkpoint; repeated fsyncs would compete with its writes. */
        bool transition = strcmp(json_text(p, "state"), state) || strcmp(json_text(p, "message"), message) ||
            (cid && strcmp(json_text(p, "installTask"), cid));
        if (!strcmp(state, "error") && strcmp(json_text(p, "state"), "error")) power_installation_failed();
        if (!strcmp(state, "installed") && strcmp(json_text(p, "state"), "installed")) power_download_completed();
        text(p, "state", state); text(p, "message", message);
        if (cid) text(p, "installTask", cid);
        if (progress) { number(p, "received", (double)progress->received); number(p, "total", (double)progress->total); }
        dirty |= transition;
    }
    pthread_mutex_unlock(&lock);
}
extern bool library_package_waiting(void);
typedef struct { unsigned char bytes[0x440]; size_t used; uint64_t total; } StreamHeader;
static size_t stream_bytes(char *data, size_t size, size_t count, void *opaque) {
    StreamHeader *h = opaque; size_t n = size * count;
    if (n > sizeof h->bytes - h->used) return 0;
    memcpy(h->bytes + h->used, data, n); h->used += n; return n;
}
static size_t stream_headers(char *data, size_t size, size_t count, void *opaque) {
    StreamHeader *h = opaque; size_t n = size * count; char line[256];
    if (n < sizeof line) { memcpy(line, data, n); line[n] = 0;
        unsigned long long first, last, total;
        if (!strncasecmp(line, "Content-Range:", 14) && sscanf(line + 14, " bytes %llu-%llu/%llu", &first, &last, &total) == 3 && first == 0 && last == 0x43f) h->total = total;
    }
    return n;
}
static void submit_stream(cJSON *r, cJSON *p) {
    const char *rid = json_text(r, "id"), *pid = json_text(p, "id");
    char error[256] = "", cid[49] = ""; StreamHeader h = {0}; long status = 0;
    CURL *curl = curl_easy_init(); CURLcode result = CURLE_FAILED_INIT;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, json_text(r, "streamUrl"));
        curl_easy_setopt(curl, CURLOPT_PROXY, ""); curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_RANGE, "0-1087");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_bytes); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &h);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, stream_headers); curl_easy_setopt(curl, CURLOPT_HEADERDATA, &h);
        result = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status); curl_easy_cleanup(curl);
    }
    /* The PS4 installer reads the package over HTTP from Orbit Zero as a
     * download; it needs the content ID, type and size from the header first. */
    bool ps5 = !memcmp(h.bytes, "\x7f" "FIH", 4), valid = !memcmp(h.bytes, "\x7f" "CNT", 4);
    if (valid) {
        memcpy(cid, h.bytes + 0x40, 36); cid[36] = 0;
        uint64_t declared = 0; for (int i = 0; i < 8; i++) declared = (declared << 8) | h.bytes[0x430 + i];
        valid = content_id_ok(cid) && (!declared || declared == h.total);
        ps5 = ps5 || !strncmp(cid + 7, "PPSA", 4);
    }
    if (result == CURLE_OK && status == 206 && h.used == sizeof h.bytes && ps5) {
        package_result(rid, pid, "error", "This is a PS5 package; it can't be installed on PS4.", NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    if (result != CURLE_OK || status != 206 || h.used != sizeof h.bytes || h.total != (uint64_t)json_int(p, "sizeBytes") || !valid) {
        package_result(rid, pid, "error", "Could not read the computer package. Keep Orbit Zero open and allow it through your firewall, then try again.", NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    /* The installer reads the package as a download, so it needs the package
     * type (content type at 0x74) and whether it is a patch (flags at 0x78). */
    uint32_t content_type = 0, flags = 0;
    for (int i = 0; i < 4; i++) { content_type = (content_type << 8) | h.bytes[0x74 + i]; flags = (flags << 8) | h.bytes[0x78 + i]; }
    PackageRemote remote = {.size = h.total,
        .package_type = content_type == 0x1A ? "PS4GD" : content_type == 0x1B ? "PS4AC" :
                        content_type == 0x1C ? "PS4AL" : content_type == 0x1E ? "PS4DP" : NULL,
        /* First, subsequent, delta and cumulative patch flags. */
        .patch = (flags & 0x61100000u) != 0};
    if (!remote.package_type) {
        package_result(rid, pid, "error", "This package can't be installed from your computer. In Orbit Zero, send it to the PS4 instead.", NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    pthread_mutex_lock(&lock); cJSON *live = find_package(find(rid), pid);
    if (live) { text(live, "state", "installing"); text(live, "message", "Installing from your computer. Keep Orbit Zero open."); text(live, "contentId", cid); text(live, "platform", "ps4");
        cJSON_DeleteItemFromObjectCaseSensitive(live, "installTask"); }
    int saved = live ? persist() : -1; pthread_mutex_unlock(&lock);
    if (saved) {
        package_result(rid, pid, "error", "Could not save installation progress. No installation was started.", NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    char task[49] = "";
    int rc = package_platform_submit_url(json_text(r, "streamUrl"), json_text(r, "title"), cid, &remote, task, error, sizeof error);
    package_result(rid, pid, rc ? "error" : *task ? "installing" : "unconfirmed",
        rc ? error : *task ? "Installing from your computer. Keep Orbit Zero open." : "The PS4 accepted this package but did not provide progress. Check the home screen.", *task ? task : NULL, NULL);
    if (rc) notify_install(json_text(r, "title"), false, 0);
}
static void submit(cJSON *r, cJSON *p) {
    if (yes(r, "stream")) { submit_stream(r, p); return; }
    /* A Library move accepted before Install must finish first. New moves of
     * this source are already blocked by the queued package's path guard. */
    if (library_package_waiting()) return;
    orbit_trace("install: checking the package file");
    const char *rid = json_text(r, "id"), *pid = json_text(p, "id");
    struct stat st; char error[256] = "", cid[49] = "", path[2400];
    int fd = open_file(r, json_text(p, "filename"), &st);
    cJSON *checked = cJSON_CreateObject();
    int erc = fd < 0 || !stamp_matches(p, &st) ? -1 : examine(fd, checked, &st);
    if (erc || !strcmp(json_text(checked, "state"), "blocked")) {
        if (fd >= 0) close(fd);
        const char *why = erc == 2
            ? "This is a PS5 package; it can't be installed on PS4."
            : "The package or drive changed. Reconnect its original drive and check the downloaded file before installing.";
        cJSON_Delete(checked);
        package_result(rid, pid, "error", why, NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    char content_id[49]; copy_text(content_id, sizeof content_id, json_text(checked, "contentId"));
    cJSON_Delete(checked);
    receipt_path(r, json_text(p, "filename"), path, sizeof path);
    /* Save 'installing' before the side effect. A crash thereafter never causes
     * an automatic second installation. An open descriptor pins the file. */
    pthread_mutex_lock(&lock); cJSON *live = find_package(find(rid), pid);
    if (live) { text(live, "state", "installing"); text(live, "message", "Installing on PS4…");
        text(live, "contentId", content_id); cJSON_DeleteItemFromObjectCaseSensitive(live, "installTask"); }
    int saved = live ? persist() : -1; pthread_mutex_unlock(&lock);
    if (saved) {
        close(fd); package_result(rid, pid, "error", "Could not save installation progress. No installation was started.", NULL, NULL);
        notify_install(json_text(r, "title"), false, 0); return;
    }
    orbit_trace("install: handing the package to the PS4");
    int rc = package_platform_submit(path, json_text(r, "title"), content_id, cid, error, sizeof error);
    orbit_trace("install: handed over 0x%08x", (unsigned)rc);
    close(fd);
    package_result(rid, pid, rc ? "error" : *cid ? "installing" : "unconfirmed",
        rc ? error : *cid ? "Installing on PS4…" : "The PS4 accepted this package but did not provide progress. Check the home screen.", *cid ? cid : NULL, NULL);
    if (rc) notify_install(json_text(r, "title"), false, 0);
}
static bool delete_installed(const cJSON *r, const cJSON *p) {
    char leaf[256]; struct stat st;
    int parent = open_parent(r, json_text(p, "filename"), leaf);
    if (parent < 0) return false;
    if (fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW)) {
        /* A file already removed by the owner needs no further cleanup. */
        int saved = errno;
        close(parent); return saved == ENOENT || saved == ENODEV;
    }
    bool same = S_ISREG(st.st_mode) && st.st_nlink == 1 && stamp_matches(p, &st);
    bool deleted = same && !unlinkat(parent, leaf, 0);
    close(parent); return deleted;
}
static void poll(cJSON *r, cJSON *p) {
    PackageProgress progress;
    int rc = package_platform_poll(json_text(p, "installTask"), &progress);
    const char *state = "installing", *message = "Installing on PS4…"; char error[256];
    int cleanup = 0; /* for the notification: kept by choice, deleted, or kept */
    if (rc) {
        pthread_mutex_lock(&lock); cJSON *live = find_package(find(json_text(r, "id")), json_text(p, "id"));
        int errors = (int)json_int(live, "pollErrors") + 1;
        if (live) number(live, "pollErrors", errors);
        pthread_mutex_unlock(&lock);
        if (errors < 5) return;
        state = "unconfirmed"; message = "Orbit could not confirm installation progress. Check the PS4 home screen, then select Check installation.";
    } else if (progress.error || !strcmp(progress.state, "error") || !strcmp(progress.state, "failed") || !strcmp(progress.state, "cancelled") || !strcmp(progress.state, "stopped")) {
        /* The PS4's download installer stops some packages at their game data
         * (0x80990004: Toby and Super Skull Smash GO! 2 Turbo, 16 Nov 2026); the
         * same files install after a normal send. Say so instead of the code. */
        if (yes(r, "stream") && (uint32_t)progress.error == 0x80990004u)
            copy_text(error, sizeof error, "This package can't be installed from your computer. In Orbit Zero, send it to the PS4 instead.");
        else if (*progress.message) copy_text(error, sizeof error, progress.message);
        else snprintf(error, sizeof error, "Package installation failed (0x%08x). Check the PS4 Downloads screen for details.", (unsigned)progress.error);
        state = "error"; message = error;
    } else if (!strcmp(progress.state, "completed") || (!strcmp(progress.state, "playable") && progress.total && progress.received >= progress.total)) {
        state = "installed"; message = "Installed on PS4. The downloaded package was kept.";
        pthread_mutex_lock(&orbit.mutex); bool remove = orbit.delete_packages && !yes(r, "stream"); pthread_mutex_unlock(&orbit.mutex);
        if (yes(r, "stream")) message = "Installed on PS4. Your computer copy was kept.";
        if (remove) cleanup = delete_installed(r, p) ? 1 : 2;
        if (remove) message = cleanup == 1 ? "Installed on PS4. The downloaded package was deleted."
            : "Installed on PS4. The downloaded package was kept because it changed or could not be deleted.";
    }
    if (!rc) { pthread_mutex_lock(&lock); cJSON *live = find_package(find(json_text(r, "id")), json_text(p, "id")); if (live) number(live, "pollErrors", 0); pthread_mutex_unlock(&lock); }
    package_result(json_text(r, "id"), json_text(p, "id"), state, message, NULL, rc ? NULL : &progress);
    if (!strcmp(state, "installed") || !strcmp(state, "error"))
        notify_install(json_text(r, "title"), !strcmp(state, "installed"), cleanup);
}
/* Library's bounded loopback calls. No package/queue lock is held while they run.
 * manual: the game is outside a homebrew folder, so ShadowMount adds it by path. */
extern int library_scan_completed(const char *path, bool request, bool manual, char *message, size_t cap);
/* Files' installs: once checked, a receipt's packages queue at once. */
static void install_checked(cJSON *r) {
    if (!yes(r, "autoInstall")) return;
    /* Installs after another download: advance_chains() queues it when that one is installed. */
    if (*json_text(r, "installAfter")) return;
    cJSON_DeleteItemFromObjectCaseSensitive(r, "autoInstall");
    if (strcmp(json_text(r, "state"), "ready")) return;
    cJSON *p;
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
        if (strcmp(json_text(p, "state"), "ready")) continue;
        if (package_platform_available()) { text(p, "state", "queued"); text(p, "message", "Waiting to install…"); }
        else { text(p, "state", "error"); text(p, "message", "Package installation is available on a PS4 running the matching Orbit ELF."); }
    }
}
/* Get everything: a checked receipt that installs after another download waits until
 * that download's packages are installed, then queues its own. If that one failed, was
 * cancelled or was left for the user to install, it stops waiting and says so; nothing
 * installs automatically then. A download removed from history after it finished no
 * longer holds it back. Queue -> package lock order, as prune_download_history(). */
static void advance_chains(void) {
    pthread_mutex_lock(&orbit.mutex);
    pthread_mutex_lock(&lock);
    cJSON *r;
    if (receipts && !broken && !stopping) cJSON_ArrayForEach(r, receipts) {
        const char *after = json_text(r, "installAfter");
        if (!yes(r, "autoInstall") || !*after || strcmp(json_text(r, "state"), "ready")) continue;
        const cJSON *prior = find_origin(after), *p;
        const Job *job = job_find(after);
        char title[512] = "", message[640];
        if (prior) copy_text(title, sizeof title, json_text(prior, "title"));
        else if (job) release_download_title(&job->release, title, sizeof title);
        int verdict = 1; /* 1 install now, 0 keep waiting, -1 stop waiting */
        if (prior) {
            int total = 0, installed = 0, stopped = 0;
            cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(prior, "packages")) {
                const char *state = json_text(p, "state");
                total++;
                if (!strcmp(state, "installed")) installed++;
                else if (!strcmp(state, "error") || !strcmp(state, "blocked") ||
                         (!strcmp(state, "ready") && !yes(prior, "autoInstall"))) stopped++;
            }
            const char *state = json_text(prior, "state");
            if (!strcmp(state, "error") || stopped) verdict = -1;
            else if (!strcmp(state, "checking") || installed < total) verdict = 0;
        } else if (job) verdict = !strcmp(job->status, "cancelled") ? -1 : 0;
        if (verdict == 1) {
            cJSON_DeleteItemFromObjectCaseSensitive(r, "installAfter");
            text(r, "chain", ""); text(r, "chainTitle", ""); text(r, "message", "");
            install_checked(r);
            dirty = true;
        } else if (verdict < 0) {
            cJSON_DeleteItemFromObjectCaseSensitive(r, "autoInstall");
            snprintf(message, sizeof message, "Install %s first, then select Install.", title);
            text(r, "chain", "blocked"); text(r, "chainTitle", title); text(r, "message", message);
            dirty = true;
        } else if (strcmp(json_text(r, "chain"), "waiting") || strcmp(json_text(r, "chainTitle"), title)) {
            snprintf(message, sizeof message, "Installs after %s.", title);
            text(r, "chain", "waiting"); text(r, "chainTitle", title); text(r, "message", message);
            dirty = true;
        }
    }
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&orbit.mutex);
}
static void *run(void *unused) {
    (void)unused;
    unsigned cursor = 0;
    while (true) {
        prune_download_history();
        advance_chains();
        pthread_mutex_lock(&lock);
        if (stopping) { if (dirty) persist(); pthread_mutex_unlock(&lock); break; }
        cJSON *record = NULL, *package = NULL, *r, *p; const char *action = "";
        unsigned count = (unsigned)cJSON_GetArraySize(receipts);
        r = count ? cJSON_GetArrayItem(receipts, (int)(cursor % count)) : NULL;
        for (unsigned n = 0; n < count; n++, r = r->next ? r->next : receipts->child) {
            ++cursor;
            if (yes(r, "removingFiles")) continue;
            cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
                if (!strcmp(json_text(p, "state"), "queued") || !strcmp(json_text(p, "state"), "installing")) {
                    record = r; package = p; action = !strcmp(json_text(p, "state"), "queued") ? "submit" : "poll"; break;
                }
            }
            if (record) break;
            if (!strcmp(json_text(r, "state"), "checking")) { record = r; action = "inspect"; break; }
            if ((!strcmp(json_text(r, "shadowMount"), "pending") || !strcmp(json_text(r, "shadowMount"), "requested")) &&
                time(NULL) >= json_int(r, "scanAfter")) { record = r; action = "scan"; break; }
        }
        if (dirty) persist();
        cJSON *copy = record ? cJSON_Duplicate(record, true) : NULL;
        cJSON *copy_package = package ? cJSON_Duplicate(package, true) : NULL;
        pthread_mutex_unlock(&lock);
        if (copy) {
            /* One line per change of step, so a wait shows where it is. */
            static char last_step[96];
            char step[96];
            snprintf(step, sizeof step, "%s %.40s", action, json_text(copy, "id"));
            if (strcmp(step, last_step)) { orbit_trace("installer worker: %s", step); copy_text(last_step, sizeof last_step, step); }
            if (!strcmp(action, "inspect")) {
                inspect(copy);
                pthread_mutex_lock(&lock); cJSON *live = find(json_text(copy, "id"));
                if (live) {
                    text(live, "device", json_text(copy, "device")); text(live, "inode", json_text(copy, "inode"));
                    text(live, "state", json_text(copy, "state")); text(live, "message", json_text(copy, "message"));
                    text(live, "shadowMount", json_text(copy, "shadowMount"));
                    cJSON_ReplaceItemInObjectCaseSensitive(live, "packages", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(copy, "packages"), true));
                    install_checked(live); dirty = true;
                }
                pthread_mutex_unlock(&lock);
            } else if (!strcmp(action, "submit")) submit(copy, copy_package);
            else if (!strcmp(action, "poll")) poll(copy, copy_package);
            else {
                char path[1900], message[256] = "";
                receipt_path(copy, json_text(copy, "output"), path, sizeof path);
                Job pin; pin_from(copy, &pin); int dir = storage_open_path(&pin, folder_of(copy)); struct stat output;
                bool present = dir >= 0 && !fstatat(dir, json_text(copy, "output"), &output, AT_SYMLINK_NOFOLLOW) &&
                    output.st_dev == pin.device && (S_ISDIR(output.st_mode) || S_ISREG(output.st_mode));
                if (dir >= 0) close(dir);
                int code = present ? library_scan_completed(path, !strcmp(json_text(copy, "shadowMount"), "pending"),
                                                            strcmp(folder_of(copy), "homebrew") != 0, message, sizeof message) : -1;
                if (!present) copy_text(message, sizeof message, "Reconnect the original drive before scanning the library.");
                pthread_mutex_lock(&lock); cJSON *live = find(json_text(copy, "id"));
                if (live) {
                    text(live, "shadowMount", code == 0 ? "registered" : code == 1 ? "requested" : code == 2 ? "pending" : "unavailable");
                    text(live, "shadowMountMessage", message); number(live, "scanAfter", (double)time(NULL) + 10);
                    int tries = (int)json_int(live, "scanChecks") + (code == 1);
                    number(live, "scanChecks", tries);
                    if (tries >= 12 && code == 1) { text(live, "shadowMount", "unconfirmed"); text(live, "shadowMountMessage", "The console has not listed this game yet. Check Library or scan again to try."); }
                    dirty = true;
                }
                pthread_mutex_unlock(&lock);
            }
        }
        cJSON_Delete(copy); cJSON_Delete(copy_package);
        pthread_mutex_lock(&lock);
        if (dirty) persist();
        if (!stopping) { struct timespec deadline = {.tv_sec = time(NULL) + 1}; pthread_cond_timedwait(&changed, &lock, &deadline); }
        pthread_mutex_unlock(&lock);
    }
    package_platform_close(); return NULL;
}
void packages_init(void) {
    pthread_mutex_lock(&lock); load(); pthread_mutex_unlock(&lock);
}
int packages_start(void) {
    pthread_mutex_lock(&lock);
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    int rc = pthread_create(&worker, &attr, run, NULL); started = !rc; pthread_attr_destroy(&attr);
    pthread_mutex_unlock(&lock);
    /* Discover packages in older completed Store jobs, without rescanning every
     * old image on the user's first launch of this version. */
    pthread_mutex_lock(&orbit.mutex);
    for (size_t i = 0; i < orbit.job_count; i++) if (!strcmp(orbit.jobs[i].status, "complete"))
        packages_completed(&orbit.jobs[i], orbit.jobs[i].id, orbit.jobs[i].output[0] ? orbit.jobs[i].output : orbit.jobs[i].filename, false);
    pthread_mutex_unlock(&orbit.mutex); return rc;
}
void packages_stop(void) {
    pthread_mutex_lock(&lock); stopping = true; pthread_cond_signal(&changed); pthread_mutex_unlock(&lock);
    if (started) pthread_join(worker, NULL);
    pthread_mutex_lock(&lock); started = false; pthread_mutex_unlock(&lock);
}
