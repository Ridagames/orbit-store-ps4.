#include "browser_platform.h"
#ifndef ORBIT_DESKTOP
#include "ps4_modules.h"
#include <string.h>
#include <pthread.h>
#ifndef ORBIT_BROWSER_PROBE
#include "orbit.h"
#define STAGE(phase, status, result) diagnostics_stage(phase, status, result)
#else
#define STAGE(phase, status, result) ((void)(result))
#define orbit_trace(...) ((void)0)
#endif

static uint32_t system_service, user_service;
static pthread_mutex_t service_lock = PTHREAD_MUTEX_INITIALIZER;
static int load_services(void) {
    if (!user_service) user_service = ps4_module_load("libSceUserService.sprx");
    return user_service ? 0 : -2;
}
static int load_browser(void) {
    int result = load_services();
    if (result) return result;
    if (!system_service)
        system_service = ps4_module_load("libSceSystemService.sprx");
    return system_service ? 0 : -1;
}
/* The user service stays initialized for the process lifetime: status
 * requests ask for the user every few seconds, and starting and stopping the
 * service each time is suspected of the freezes on 13 November 2026. */
static bool user_service_ready;
static int foreground_user(void) {
    if (load_services()) return -1;
    int (*initialize)(void *) = ps4_module_symbol(user_service, "sceUserServiceInitialize");
    int (*foreground)(int *) = ps4_module_symbol(user_service, "sceUserServiceGetForegroundUser");
    if (!initialize || !foreground) return -1;
    if (!user_service_ready) {
        int initialized = initialize(NULL);
        orbit_trace("user service initialize 0x%08x", (unsigned)initialized);
        if (initialized && (uint32_t)initialized != 0x80960003u) return -1;
        user_service_ready = true;
    }
    int user = -1;
    orbit_trace("foreground user: asking");
    if (foreground(&user)) user = -1;
    orbit_trace("foreground user: answered");
    /* Never fall back to a different initial/logged-in user. */
    return user;
}
/* Status requests ask for the user too, and the service answers requests one
 * at a time: never wait long for a launch that holds this lock. */
static bool lock_services(void) {
    for (unsigned waited = 0; waited < 2000; waited += 10) {
        if (!pthread_mutex_trylock(&service_lock)) return true;
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
    return false;
}
int probe_browser_user(void) {
    if (!lock_services()) return -1;
    int user = foreground_user();
    pthread_mutex_unlock(&service_lock);
    return user;
}

/* Only the explicit browser-session worker calls this. Keep loaded handles for
 * the process lifetime: opening the browser is asynchronous. No native service
 * import, lookup or initialization belongs on Orbit's normal startup path. */
static int open_browser(const char *url) {
    if (!url || !*url) return -1;
    STAGE("browser-modules", "started", 0);
    int loaded = load_browser();
    if (loaded) {
        STAGE("browser-modules", "error", loaded);
        return loaded;
    }
    int (*initialize)(void *) = ps4_module_symbol(user_service, "sceUserServiceInitialize");
    int (*terminate)(void) = ps4_module_symbol(user_service, "sceUserServiceTerminate");
    int (*launch)(const char *, void *) =
        ps4_module_symbol(system_service, "sceSystemServiceLaunchWebBrowser");
    if (!initialize || !terminate || !launch) {
        STAGE("browser-modules", "error", -3);
        return -3;
    }
    STAGE("browser-modules", "ok", 0);
    STAGE("browser-user-service", "started", 0);
    int result = initialize(NULL);
    STAGE("browser-user-service", result ? "error" : "ok", result);
    if (result && (uint32_t)result != 0x80960003u) return result;
    int owned = !result;
    STAGE("browser-launch", "started", 0);
    result = launch(url, NULL);
    STAGE("browser-launch", result ? "error" : "ok", result);
    STAGE("browser-user-cleanup", "started", 0);
    int cleanup = owned ? terminate() : 0;
    STAGE("browser-user-cleanup", cleanup ? "error" : "ok", cleanup);
    return result;
}
int probe_browser_open(const char *url) {
    pthread_mutex_lock(&service_lock);
    int result = open_browser(url);
    pthread_mutex_unlock(&service_lock);
    return result;
}

/* The launch parameters ShellCore's own launcher takes; foreground user only. */
typedef struct {
    uint32_t size;
    int32_t user;
    uint32_t options;
    uint64_t crash_report;
    uint32_t check_flag;
} LaunchParam;
/* Starts one of Orbit's own titles (the browser helper, or the TV app to
 * return to it). A running title is brought to the front. */
int probe_app_launch(const char *title_id) {
    if (!title_id || strlen(title_id) != 9 || strncmp(title_id, "ORBT", 4)) return -1;
    if (!lock_services()) return -2;
    int result = load_browser();
    int (*launch)(const char *, const char **, LaunchParam *) =
        result ? NULL : ps4_module_symbol(system_service, "sceLncUtilLaunchApp");
    int user = result ? -1 : foreground_user();
    pthread_mutex_unlock(&service_lock);
    if (!result && (!launch || user < 0)) result = -3;
    if (!result) {
        /* Outside the lock: the system's launcher can take its time. */
        LaunchParam param = {.size = sizeof param, .user = user};
        result = launch(title_id, NULL, &param);
        STAGE("app-launch", result < 0 ? "error" : "ok", result);
        if (result > 0) result = 0; /* the new application's id */
    }
    return result;
}

static bool own_title(const char *title_id) {
    return title_id && strlen(title_id) == 9 && !strncmp(title_id, "ORBT", 4) &&
           strspn(title_id + 4, "0123456789") == 5;
}
/* The application id of a running title, 0 when it is not running, negative
 * when the console can't say. Firmware 12.00 has sceLncUtilGetAppId; the mini
 * app lookup (the TV app is a mini app) is the fallback. */
static int running_app_id(const char *title_id) {
    if (load_browser()) return -1;
    int (*by_title)(const char *) = ps4_module_symbol(system_service, "sceLncUtilGetAppId");
    if (by_title) {
        int id = by_title(title_id);
        orbit_trace("app %s: id 0x%08x", title_id, (unsigned)id);
        /* An error code means the title is not running. */
        return id > 0 ? id : 0;
    }
    int (*mini)(void) = ps4_module_symbol(system_service, "sceSystemServiceGetAppIdOfMiniApp");
    int (*title_of)(int, char *) = ps4_module_symbol(system_service, "sceLncUtilGetAppTitleId");
    if (!mini || !title_of) return -1;
    int id = mini();
    char title[16] = {0};
    if (id <= 0 || title_of(id, title)) return 0;
    return !strncmp(title, title_id, 9) ? id : 0;
}
int probe_app_running(const char *title_id) {
    if (!own_title(title_id) || !lock_services()) return -1;
    int id = running_app_id(title_id);
    pthread_mutex_unlock(&service_lock);
    return id < 0 ? id : id > 0;
}
int probe_app_close(const char *title_id) {
    if (!own_title(title_id) || !lock_services()) return -1;
    int id = running_app_id(title_id), result = id < 0 ? -1 : 0;
    if (id > 0) {
        int (*kill_app)(int) = ps4_module_symbol(system_service, "sceLncUtilKillApp");
        int (*kill_system)(int, int, int, int) = ps4_module_symbol(system_service, "sceSystemServiceKillApp");
        result = kill_app ? kill_app(id) : kill_system ? kill_system(id, -1, 0, 0) : -3;
        orbit_trace("app %s: close 0x%08x", title_id, (unsigned)result);
        STAGE("app-close", result ? "error" : "ok", result);
    }
    pthread_mutex_unlock(&service_lock);
    /* Wait outside the lock: status requests ask for the user meanwhile. */
    for (unsigned waited = 0; !result && waited < 10000; waited += 250) {
        int running = probe_app_running(title_id);
        if (!running) return 0;
        if (running < 0) result = -1;
        struct timespec pause = {.tv_nsec = 250000000L};
        nanosleep(&pause, NULL);
    }
    return result ? result : -4;
}

#else
int probe_browser_open(const char *url) { (void)url; return -1; }
int probe_browser_user(void) { return -1; }
#ifdef ORBIT_TEST
/* Tests drive these through files in ORBIT_TEST_PACKAGE_STATE_DIR:
 * running-<title> marks a running title; launch and close leave
 * launched-<title> and closed-<title> behind. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool test_marker(const char *kind, const char *title, int action) {
    const char *dir = getenv("ORBIT_TEST_PACKAGE_STATE_DIR");
    char path[1200];
    if (!dir || !*dir || !title || strlen(title) != 9 ||
        snprintf(path, sizeof path, "%s/%s-%s", dir, kind, title) >= (int)sizeof path) return false;
    if (action > 0) { int fd = open(path, O_WRONLY | O_CREAT, 0644); if (fd >= 0) close(fd); return fd >= 0; }
    if (action < 0) return !unlink(path);
    return !access(path, F_OK);
}
int probe_app_launch(const char *title_id) { return test_marker("launched", title_id, 1) ? 0 : -1; }
int probe_app_running(const char *title_id) {
    return getenv("ORBIT_TEST_PACKAGE_STATE_DIR") ? test_marker("running", title_id, 0) : -1;
}
int probe_app_close(const char *title_id) {
    if (getenv("ORBIT_TEST_APP_CLOSE_FAILS")) return -1;
    test_marker("running", title_id, -1);
    return test_marker("closed", title_id, 1) ? 0 : -1;
}
#else
int probe_app_launch(const char *title_id) { (void)title_id; return -1; }
int probe_app_running(const char *title_id) { (void)title_id; return -1; }
int probe_app_close(const char *title_id) { (void)title_id; return -1; }
#endif
#endif
