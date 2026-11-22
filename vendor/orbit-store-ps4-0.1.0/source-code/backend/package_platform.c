/* PS4 local package installation through BGFT. Storage tasks copy the input,
 * keep a stable task id and report their own progress (base, patch and DLC).
 * AppExists is a separate query: an installed base never proves an update ran.
 * ABI: flatz/ps4_stub_lib_maker_v2/include/bgft.h; storage registration sequence:
 * cy33hc/ps4-ezremote-client/source/installer.cpp. */
#include "packages.h"
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef ORBIT_DESKTOP
#include "ps4_bgft.h"
#include "ps4_modules.h"
#include "browser_platform.h"
#include <errno.h>
#include <limits.h>
#include <time.h>
static int (*bgft_init)(Ps4BgftInit *), (*bgft_term)(void);
static int (*register_task)(Ps4BgftDownloadEx *, int *);
/* Packages read over HTTP register as a download task, except patches (any
 * patch flag in the header) as a debug package, as ezRemote Client does. */
static int (*register_url_task)(Ps4BgftDownload *, int *), (*register_url_patch)(Ps4BgftDownload *, int *);
static int (*start_task)(int), (*unregister_task)(int);
static int (*get_progress)(int, Ps4BgftProgress *);
static int (*app_init)(void), (*app_term)(void), (*app_exists)(const char *, int *);
static bool bgft_initialized, app_initialized;
static uint32_t app_module;
static Ps4BgftInit bgft_heap;
static pthread_mutex_t platform_lock = PTHREAD_MUTEX_INITIALIZER;

/* Web requests run one at a time, so a request must never wait for the
 * console's installer indefinitely. Submitting a package holds this lock while
 * the console registers it; on firmware 12.00 Orbit stopped answering after
 * the console refused a package that was already installed. */
static bool lock_within(pthread_mutex_t *lock, unsigned milliseconds) {
    for (unsigned waited = 0;; waited += 10) {
        if (!pthread_mutex_trylock(lock)) return true;
        if (waited >= milliseconds) return false;
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
}

static int prepare_bgft(void) {
    if (bgft_initialized) return 0;
    uint32_t handle = ps4_module_load("libSceBgft.sprx");
    bgft_init = ps4_module_symbol(handle, "sceBgftServiceIntInit");
    bgft_term = ps4_module_symbol(handle, "sceBgftServiceIntTerm");
    register_task = ps4_module_symbol(handle, "sceBgftServiceIntDownloadRegisterTaskByStorageEx");
    start_task = ps4_module_symbol(handle, "sceBgftServiceDownloadStartTask");
    get_progress = ps4_module_symbol(handle, "sceBgftServiceDownloadGetProgress");
    unregister_task = ps4_module_symbol(handle, "sceBgftServiceIntDownloadUnregisterTask");
    register_url_task = ps4_module_symbol(handle, "sceBgftServiceIntDownloadRegisterTask");
    register_url_patch = ps4_module_symbol(handle, "sceBgftServiceIntDebugDownloadRegisterPkg");
    if (!bgft_init || !bgft_term || !register_task || !start_task || !get_progress) return -1;
    bgft_heap.heap_size = 1024 * 1024;
    bgft_heap.heap = calloc(1, bgft_heap.heap_size);
    if (!bgft_heap.heap) return -1;
    int code = bgft_init(&bgft_heap);
    if (!code) bgft_initialized = true;
    else { free(bgft_heap.heap); memset(&bgft_heap, 0, sizeof bgft_heap); }
    return code;
}
static int prepare_apps(void) {
    if (app_initialized) return 0;
    app_module = ps4_module_load("libSceAppInstUtil.sprx");
    app_init = ps4_module_symbol(app_module, "sceAppInstUtilInitialize");
    app_term = ps4_module_symbol(app_module, "sceAppInstUtilTerminate");
    app_exists = ps4_module_symbol(app_module, "sceAppInstUtilAppExists");
    if (!app_init || !app_term || !app_exists) return -1;
    int code = app_init();
    if (!code) app_initialized = true;
    return code;
}
bool package_platform_available(void) { return true; }
int package_platform_installed(const char *title_id, bool *installed) {
    if (!installed || !title_id || strlen(title_id) != 9) return -1;
    *installed = false;
    /* Unknown while an installation is being registered; callers fall back. */
    if (!lock_within(&platform_lock, 2000)) return -2;
    int flag = 0, code = prepare_apps();
    if (!code) code = app_exists(title_id, &flag);
    if (!code) *installed = flag != 0;
    pthread_mutex_unlock(&platform_lock);
    return code;
}
/* Removes an installed application. Only used for Orbit's own helper, which
 * holds no user data, before installing a newer copy of it. */
int package_platform_uninstall(const char *title_id) {
    if (!title_id || strlen(title_id) != 9) return -1;
    if (!lock_within(&platform_lock, 2000)) return -2;
    int code = prepare_apps();
    int (*uninstall)(const char *) = code ? NULL : ps4_module_symbol(app_module, "sceAppInstUtilAppUnInstall");
    if (!code && !uninstall) code = -1;
    if (!code) code = uninstall(title_id);
    pthread_mutex_unlock(&platform_lock);
    return code;
}
static int submit_task(const char *path, const char *name, const char *content_id, const PackageRemote *remote,
                       char result_id[49], char *error, size_t cap) {
    if (!result_id) return -1;
    *result_id = 0;
    if (!path || !*path || !content_id || !*content_id || strlen(content_id) > 48 ||
        (remote && (!remote->package_type || !*remote->package_type || !remote->size))) {
        copy_text(error, cap, "The package path or content ID is invalid.");
        return -1;
    }
    char uri[2048];
    /* The system service resolves the data mount through /user. USB stays on USB. */
    if (snprintf(uri, sizeof uri, "%s%s", !remote && !strncmp(path, "/data/", 6) ? "/user" : "", path) >= (int)sizeof uri) {
        copy_text(error, cap, "The package path is too long for the installer.");
        return -1;
    }
    /* Ask before taking the installer lock; -1 (unknown) leaves the default user. */
    int user = remote ? probe_browser_user() : -1;
    orbit_trace("install: waiting for the installer lock");
    pthread_mutex_lock(&platform_lock);
    orbit_trace("install: preparing the installer");
    int code = prepare_bgft();
    orbit_trace("install: installer ready 0x%08x", (unsigned)code);
    if (code) {
        pthread_mutex_unlock(&platform_lock);
        snprintf(error, cap, "The PS4 package installer is unavailable (0x%08x). Check your jailbreak and try again.", (unsigned)code);
        return code;
    }
    Ps4BgftDownloadEx params = {0};
    params.params.entitlement_type = 5;
    params.params.id = content_id;
    params.params.content_url = uri;
    params.params.content_name = name && *name ? name : content_id;
    params.params.icon_path = "";
    params.params.playgo_scenario_id = "0";
    params.params.option = 8; /* SCE_BGFT_TASK_OPT_FORCE_UPDATE */
    int task = -1;
    if (remote) {
        /* A storage task refuses an HTTP address (0x8099006a, 15 Nov 2026):
         * a download task needs the package type and size from its header. */
        int (*reg)(Ps4BgftDownload *, int *) = remote->patch ? register_url_patch : register_url_task;
        if (!reg) {
            pthread_mutex_unlock(&platform_lock);
            copy_text(error, cap, "This PS4 can't install packages from your computer. In Orbit Zero, send the package to the PS4 instead.");
            return -1;
        }
        if (user >= 0) params.params.user_id = user;
        params.params.option = 0x10000; /* SCE_BGFT_TASK_OPT_DISABLE_CDN_QUERY_PARAM */
        params.params.package_type = remote->package_type;
        params.params.package_sub_type = "";
        params.params.package_size = remote->size;
        orbit_trace("install: registering the computer package (%s%s)", remote->package_type, remote->patch ? ", patch" : "");
        code = reg(&params.params, &task);
    } else {
        orbit_trace("install: registering the package");
        code = register_task(&params, &task);
    }
    orbit_trace("install: registered 0x%08x task %d", (unsigned)code, task);
    if (!code && task < 0) code = -1;
    if (!code) {
        code = start_task(task);
        orbit_trace("install: started 0x%08x", (unsigned)code);
        if (code && unregister_task) unregister_task(task);
    }
    if (!code) snprintf(result_id, 49, "bgft:%d", task);
    pthread_mutex_unlock(&platform_lock);
    if ((uint32_t)code == 0x80990088u || (uint32_t)code == 0x80990015u)
        copy_text(error, cap, "The console refused to replace an installed package. The existing installation and downloaded file were kept.");
    else if (code)
        snprintf(error, cap, "The PS4 could not install this package (0x%08x). Check free space, package compatibility and your jailbreak. The downloaded file was kept.", (unsigned)code);
    return code;
}
int package_platform_submit(const char *path, const char *name, const char *content_id,
                            char result_id[49], char *error, size_t cap) {
    return submit_task(path, name, content_id, NULL, result_id, error, cap);
}
int package_platform_submit_url(const char *url, const char *name, const char *content_id,
                                const PackageRemote *remote, char result_id[49], char *error, size_t cap) {
    if (!remote || !url || strncmp(url, "http://", 7)) {
        copy_text(error, cap, "The package address is invalid.");
        return -1;
    }
    return submit_task(url, name, content_id, remote, result_id, error, cap);
}
int package_platform_poll(const char *result_id, PackageProgress *result) {
    if (!result) return -1;
    memset(result, 0, sizeof *result);
    /* Old title-id receipts are unconfirmed, not successful installations. */
    if (!result_id || strncmp(result_id, "bgft:", 5) || result_id[5] < '0' || result_id[5] > '9') return -1;
    errno = 0;
    char *end;
    long task = strtol(result_id + 5, &end, 10);
    if (errno || *end || task < 0 || task > INT_MAX) return -1;
    pthread_mutex_lock(&platform_lock);
    int code = prepare_bgft();
    Ps4BgftProgress progress = {0};
    if (!code) code = get_progress((int)task, &progress);
    /* A failed task stays in the PS4's download list and makes the console
     * refuse the same package again (0x80990088, 16 Nov 2026): remove it. */
    if (!code && progress.error_result && unregister_task) unregister_task((int)task);
    pthread_mutex_unlock(&platform_lock);
    if (code) return code;
    result->total = progress.length_total ? progress.length_total : progress.length;
    result->received = progress.length_total ? progress.transferred_total : progress.transferred;
    result->error = progress.error_result;
    if (result->error) {
        copy_text(result->state, sizeof result->state, "error");
        snprintf(result->message, sizeof result->message,
                 "The PS4 package installation failed (0x%08x). The downloaded file was kept.", (unsigned)result->error);
    } else {
        /* Zero length is preparation, never 100%. Use 64-bit bytes for >4 GiB games. */
        bool complete = result->total && result->received >= result->total;
        copy_text(result->state, sizeof result->state, complete ? "completed" : "installing");
    }
    return 0;
}
void package_platform_close(void) {
    pthread_mutex_lock(&platform_lock);
    if (bgft_initialized) bgft_term();
    if (app_initialized) app_term();
    bgft_initialized = app_initialized = false;
    free(bgft_heap.heap);
    memset(&bgft_heap, 0, sizeof bgft_heap);
    pthread_mutex_unlock(&platform_lock);
}

#else
/* No installation emulation in normal desktop builds. Test-only deterministic
 * service responses exercise the real receipt/API/worker without a console.
 * ORBIT_TEST_PACKAGE_INSTALL selects a mode: set submit succeeds and later polls
 * report "completed" (the package became installed); "installed" reports an
 * already-installed title; "conflict" refuses an installed title the way the
 * PS4 does (0x80990088) until it is uninstalled; "hold"/"error"/"lost"/"reject"
 * force those states. */
#ifdef ORBIT_TEST
static char submitted_title[49], submitted_task[49];
static unsigned next_task;
static void copy_test_install(const char *source, const char *title) {
    if (!*orbit.system_root || strlen(title) != 9 || strspn(title, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != 9) return;
    char dir[1400], target[1500], temp[1510];
    snprintf(dir, sizeof dir, "%s/user", orbit.system_root); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/user/app", orbit.system_root); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/user/app/%s", orbit.system_root, title); mkdir(dir, 0755);
    snprintf(target, sizeof target, "%s/app.pkg", dir);
    snprintf(temp, sizeof temp, "%s.part", target);
    FILE *in = fopen(source, "rb"), *out = fopen(temp, "wb");
    bool ok = in && out;
    if (ok) { char buffer[16384]; size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, in)))
            if (fwrite(buffer, 1, n, out) != n) { ok = false; break; }
        if (ferror(in)) ok = false;
    }
    if (in) fclose(in);
    if (out && fclose(out)) ok = false;
    if (ok) rename(temp, target); else unlink(temp);
}
/* ORBIT_TEST_INSTALL_SETTLE_SECONDS: the package appears that long after the
 * task completes, as on a PS4, where a second short task moves it into place. */
typedef struct { char source[1024], title[49]; unsigned seconds; } LateCopy;
static void *late_copy(void *argument) {
    LateCopy *c = argument;
    sleep(c->seconds);
    copy_test_install(c->source, c->title);
    free(c);
    return NULL;
}
static void place_test_install(const char *source, const char *title) {
    const char *settle = getenv("ORBIT_TEST_INSTALL_SETTLE_SECONDS");
    LateCopy *c = settle && atoi(settle) > 0 ? calloc(1, sizeof *c) : NULL;
    pthread_t thread;
    if (c) {
        copy_text(c->source, sizeof c->source, source);
        copy_text(c->title, sizeof c->title, title);
        c->seconds = (unsigned)atoi(settle);
        if (!pthread_create(&thread, NULL, late_copy, c)) { pthread_detach(thread); return; }
        free(c);
    }
    copy_test_install(source, title);
}
/* ORBIT_TEST_PACKAGE_STATE_DIR gives installs persistence across service restarts:
 * submit drops an <id> marker file there and poll consults it, like app.db does. */
static void mark_installed(const char *id) {
    const char *dir = getenv("ORBIT_TEST_PACKAGE_STATE_DIR");
    if (!dir || !*dir || !id || !*id) return;
    char path[1100];
    if (snprintf(path, sizeof path, "%s/%s", dir, id) >= (int)sizeof path) return;
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
}
static bool marked_installed(const char *id) {
    const char *dir = getenv("ORBIT_TEST_PACKAGE_STATE_DIR");
    if (!dir || !*dir || !id || !*id) return false;
    char path[1100];
    if (snprintf(path, sizeof path, "%s/%s", dir, id) >= (int)sizeof path) return false;
    return access(path, F_OK) == 0;
}
#endif
bool package_platform_available(void) {
#ifdef ORBIT_TEST
    return getenv("ORBIT_TEST_PACKAGE_INSTALL") != NULL;
#else
    return false;
#endif
}
int package_platform_installed(const char *title_id, bool *installed) {
    if (!title_id || !installed) return -1;
    *installed = false;
#ifdef ORBIT_TEST
    const char *mode = getenv("ORBIT_TEST_PACKAGE_INSTALL");
    if (!mode) return -1;
    *installed = !strcmp(mode, "installed") || marked_installed(title_id) ||
        ((!strcmp(mode, "ok")) && !strcmp(title_id, submitted_title));
    if (!strcmp(mode, "conflict")) *installed = marked_installed(title_id);
    return 0;
#else
    return -1;
#endif
}
int package_platform_submit(const char *path, const char *name, const char *content_id,
                            char result_id[49], char *error, size_t cap) {
    (void)path; (void)name;
#ifdef ORBIT_TEST
    const char *mode = getenv("ORBIT_TEST_PACKAGE_INSTALL");
    if (mode && strcmp(mode, "reject")) {
        /* Keep app presence and task progress separate, as the console does. */
        const char *dash = content_id ? strchr(content_id, '-') : NULL;
        const char *under = dash ? strchr(dash + 1, '_') : NULL;
        if (dash && under && under - dash > 1 && under - dash < 49) {
            memcpy(submitted_title, dash + 1, (size_t)(under - dash - 1));
            submitted_title[under - dash - 1] = 0;
        } else copy_text(submitted_title, 49, content_id && *content_id ? content_id : "TEST-PACKAGE");
        if (!strcmp(mode, "conflict") && marked_installed(submitted_title)) {
            copy_text(error, cap, "The console refused to replace an installed package. The existing installation and downloaded file were kept.");
            return (int)0x80990088u;
        }
        snprintf(submitted_task, sizeof submitted_task, "bgft:%u", ++next_task);
        copy_text(result_id, 49, !strcmp(mode, "no-id") ? "" : submitted_task);
        if (!strcmp(mode, "ok") || !strcmp(mode, "installed") || !strcmp(mode, "conflict")) {
            place_test_install(path, submitted_title);
            mark_installed(submitted_title);
            mark_installed(submitted_task);
        }
        return 0;
    }
#else
    (void)content_id; (void)result_id;
#endif
    copy_text(error, cap, "The PS4 package installer is unavailable. The downloaded file was kept."); return -1;
}
/* Tests see what a computer install would register (ORBIT_TEST_STREAM_LOG). */
int package_platform_submit_url(const char *url, const char *name, const char *content_id,
                                const PackageRemote *remote, char result_id[49], char *error, size_t cap) {
    if (!remote || !url || strncmp(url, "http://", 7)) { copy_text(error, cap, "The package address is invalid."); return -1; }
#ifdef ORBIT_TEST
    const char *log = getenv("ORBIT_TEST_STREAM_LOG");
    FILE *f = log && *log ? fopen(log, "a") : NULL;
    if (f) { fprintf(f, "%s %llu %d\n", remote->package_type, (unsigned long long)remote->size, remote->patch ? 1 : 0); fclose(f); }
#endif
    return package_platform_submit(url, name, content_id, result_id, error, cap);
}
int package_platform_poll(const char *id, PackageProgress *result) {
    (void)id; memset(result, 0, sizeof *result);
#ifdef ORBIT_TEST
    const char *mode = getenv("ORBIT_TEST_PACKAGE_INSTALL");
    /* "error-80990004" fails like a package the PS4 can't install from a computer. */
    bool failed = mode && (!strcmp(mode, "error") || !strcmp(mode, "error-80990004"));
    if (!mode || !id || strncmp(id, "bgft:", 5) || !id[5] ||
        strspn(id + 5, "0123456789") != strlen(id + 5) || !strcmp(mode, "lost") ||
        (strcmp(mode, "installed") && strcmp(mode, "hold") && !failed &&
         strcmp(submitted_task, id) && !marked_installed(id)))
        return -1;
    copy_text(result->state, sizeof result->state, !strcmp(mode, "hold") ? "installing" : failed ? "error" : "completed");
    result->total = 4096; result->received = !strcmp(mode, "hold") ? 2048 : 4096;
    if (failed) result->error = !strcmp(mode, "error") ? -2 : (int)0x80990004u;
    return 0;
#else
    return -1;
#endif
}
void package_platform_close(void) {
#ifdef ORBIT_TEST
    *submitted_title = *submitted_task = 0;
#endif
}
int package_platform_uninstall(const char *title_id) {
#ifdef ORBIT_TEST
    const char *dir = getenv("ORBIT_TEST_PACKAGE_STATE_DIR");
    char path[1500];
    if (!getenv("ORBIT_TEST_PACKAGE_INSTALL") || getenv("ORBIT_TEST_UNINSTALL_FAILS") || !title_id ||
        strlen(title_id) != 9 || !dir || !marked_installed(title_id)) return -1;
    snprintf(path, sizeof path, "%s/%s", dir, title_id); unlink(path);
    snprintf(path, sizeof path, "%s/user/app/%s/app.pkg", orbit.system_root, title_id); unlink(path);
    snprintf(path, sizeof path, "%s/user/app/%s", orbit.system_root, title_id); rmdir(path);
    /* The app's own files go with it, as on a PS4 (its /download0 image). */
    snprintf(path, sizeof path, "%s/user/download/%s/download0.dat", orbit.system_root, title_id); unlink(path);
    return 0;
#else
    (void)title_id; return -1;
#endif
}
#endif
