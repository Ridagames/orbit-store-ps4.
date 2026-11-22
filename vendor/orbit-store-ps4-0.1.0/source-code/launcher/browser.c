/* "Orbit Store Browser" (ORBT00001), a normal PS4 application.
 *
 * Started from its home-screen tile, it opens Orbit in the Internet Browser.
 * Started by Orbit's service for a provider's download page, it opens that
 * page in Sony's web browser window instead. The window closes itself when
 * the page goes to an address containing the text the service named (the
 * provider's download route) and returns that address, which is sent back to
 * the service on this console. Nothing reads another process's memory.
 *
 * Sony's window only works for normal applications: the Orbit TV app is a
 * mini app (needed for its graphics) and gets "Cannot use this feature now".
 * Window and callback layouts were checked on firmware 12.00 on 13 November
 * 2026; see docs/ps4-test-control.md. The payload remains a separate service. */
#include "network.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern int sceKernelDebugOutText(int, const char *);
extern int sceUserServiceInitialize(void *);
extern int sceUserServiceTerminate(void);
extern int sceUserServiceGetInitialUser(int *);
extern int sceSystemServiceLaunchWebBrowser(const char *, void *);
extern int sceSystemServiceLoadExec(const char *, const char **);
extern int sceSysmoduleLoadModule(uint16_t);
extern int sceSysmoduleLoadModuleInternal(uint32_t);
extern int sceCommonDialogInitialize(void);

typedef struct { uint64_t size; uint8_t reserved[36]; uint32_t magic; } DialogBase;
typedef struct {
    DialogBase base;
    uint64_t size;
    int32_t mode, user;
    const char *url;
    void *callback;
    uint16_t width, height, x, y;
    uint32_t parts;
    uint16_t header_width, header_x, header_y, pad;
    uint32_t control;
    void *ime, *webview;
    uint32_t animation;
    uint8_t reserved[202];
    uint16_t tail;
} DialogParam;
_Static_assert(sizeof(DialogBase) == 48, "dialog base");
_Static_assert(sizeof(DialogParam) == 328, "dialog parameters");
_Static_assert(__builtin_offsetof(DialogParam, mode) == 56, "mode");
_Static_assert(__builtin_offsetof(DialogParam, url) == 64, "url");
_Static_assert(__builtin_offsetof(DialogParam, callback) == 72, "callback");
/* Type 2: the window closes when an address contains `text`. */
typedef struct { uint64_t size; int32_t type; uint32_t pad; const char *text; uint8_t reserved[32]; } CallbackInit;
_Static_assert(sizeof(CallbackInit) == 56, "callback");
/* Filled by the window: the matched address from byte 12. */
typedef struct { uint64_t size; int32_t type; char address[2084]; } CallbackResult;
_Static_assert(sizeof(CallbackResult) == 2096, "callback result");
typedef struct { int32_t result, pad; void *callback; uint8_t reserved[240]; } DialogResult;
_Static_assert(sizeof(DialogResult) == 256, "dialog result");
#define CALLBACK_MATCHED 0x1e

extern int sceWebBrowserDialogInitialize(void);
extern int sceWebBrowserDialogOpen(DialogParam *);
extern int sceWebBrowserDialogUpdateStatus(void);
extern int sceWebBrowserDialogGetResult(DialogResult *);
extern int sceWebBrowserDialogTerminate(void);
extern int sceWebBrowserDialogClose(void);

/* Never logs page or file addresses: signed links are credentials. */
static void say(const char *format, ...) {
    char line[200];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line - 1, format, args);
    va_end(args);
    strcat(line, "\n");
    sceKernelDebugOutText(0, line);
}

/* One request to Orbit's service on this console. Returns the HTTP status and
 * the body in `out`, or -1. */
static int request(const char *method, const char *path, const char *token, const char *body,
                   char *out, size_t cap) {
    out[0] = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 8};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(ORBIT_HTTP_PORT)};
    peer.sin_addr.s_addr = htonl(0x7f000001);
    if (connect(fd, (struct sockaddr *)&peer, sizeof peer)) { close(fd); return -1; }
    char head[512];
    size_t length = body ? strlen(body) : 0;
    int n = snprintf(head, sizeof head,
        "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n%s%s%s"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
        method, path, ORBIT_HTTP_PORT, token ? "Authorization: Bearer " : "", token ? token : "",
        token ? "\r\n" : "", length);
    bool sent = n > 0 && (size_t)n < sizeof head && send(fd, head, (size_t)n, 0) == n &&
                (!length || send(fd, body, length, 0) == (ssize_t)length);
    static char reply[16384];
    size_t used = 0;
    while (sent && used < sizeof reply - 1) {
        ssize_t got = recv(fd, reply + used, sizeof reply - 1 - used, 0);
        if (got <= 0) break;
        used += (size_t)got;
    }
    close(fd);
    reply[used] = 0;
    int status = -1;
    if (!sent || sscanf(reply, "HTTP/1.%*d %d", &status) != 1) return -1;
    const char *start = strstr(reply, "\r\n\r\n");
    if (start) snprintf(out, cap, "%s", start + 4);
    return status;
}

/* A top-level string field from the service's own JSON. Escapes other than
 * \" \\ \/ are refused; nothing here needs them. */
static bool field(const char *json, const char *key, char *out, size_t cap) {
    char pattern[48];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (*p++ != '"') return false;
    size_t used = 0;
    for (; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            c = *++p;
            if (c != '"' && c != '\\' && c != '/') return false;
        }
        if ((unsigned char)c < 32 || used + 1 >= cap) return false;
        out[used++] = c;
    }
    out[used] = 0;
    return *p == '"';
}

static bool quote(const char *in, char *out, size_t cap) {
    size_t used = 0;
    for (; *in; in++) {
        if ((unsigned char)*in < 32 || used + 3 >= cap) return false;
        if (*in == '"' || *in == '\\') out[used++] = '\\';
        out[used++] = *in;
    }
    out[used] = 0;
    return true;
}

static void open_orbit_in_browser(void) {
    char url[80];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", ORBIT_HTTP_PORT);
    int result = sceSystemServiceLaunchWebBrowser(url, NULL);
    say("[ORBIT-HELPER] Internet Browser 0x%08x", (unsigned)result);
}

static DialogParam param __attribute__((aligned(16)));
static CallbackInit watch_param __attribute__((aligned(16)));
static CallbackResult found __attribute__((aligned(16)));

/* The service picks the window. "custom" (mode 2) ignores Cross, so a stray
 * press can't close the page and lose the verification; Circle follows the
 * link under the pointer and the left stick moves it. It needs a size and a
 * header width, and its address watch fires for download links too
 * (13 November 2026). Anything else is the normal window (mode 1), where
 * Cross closes the page. Details: docs/ps4-test-control.md. */
/* Opens `page` in Sony's window, watching for `watch` when it is not empty. */
static bool run_window(const char *page, const char *watch, bool custom, int user) {
    if (sceSysmoduleLoadModuleInternal(0x80000018) || sceSysmoduleLoadModule(0x00ab)) return false;
    int common = sceCommonDialogInitialize();
    if (common && (uint32_t)common != 0x80b80002u) return false;
    if (sceWebBrowserDialogInitialize()) return false;
    memset(&param, 0, sizeof param);
    param.base.size = sizeof param.base;
    param.base.magic = 0xc0d1a109u + (uint32_t)(uintptr_t)&param.base;
    param.size = sizeof param;
    param.mode = 1;
    param.user = user;
    param.url = page;
    if (custom) {
        param.mode = 2;
        param.width = 1920; param.height = 1080;
        param.parts = 0x1f;
        param.header_width = 1920;
    }
    if (watch[0]) {
        memset(&watch_param, 0, sizeof watch_param);
        watch_param.size = sizeof watch_param;
        watch_param.type = 2;
        watch_param.text = watch;
        param.callback = &watch_param;
    }
    int rc = sceWebBrowserDialogOpen(&param);
    say("[ORBIT-HELPER] window open 0x%08x", (unsigned)rc);
    if (rc) { sceWebBrowserDialogTerminate(); return false; }
    return true;
}

/* Whether this helper's session is still running in Orbit. Only a clear "no"
 * counts (another session, or none active): if the service doesn't answer for
 * a moment, the window stays. */
static bool session_over(const char *token, const char *id) {
    static char status[8192];
    char current[48];
    if (request("GET", "/api/v1/browser", token, NULL, status, sizeof status) != 200) return false;
    if (strstr(status, "\"active\":false")) return true;
    return field(status, "id", current, sizeof current) && strcmp(current, id);
}

/* Waits for the window to close. True with the matched address in `address`
 * when the window found the watched text. Cancelling in Orbit (phone, browser
 * interface or TV app) or the session timing out closes the window too: it
 * used to stay open for up to 20 minutes and block the next page (PS4, 13 Nov
 * 2026). */
static bool wait_window(char *address, size_t cap, const char *token, const char *id) {
    int state = 2;
    bool closing = false;
    /* Twenty minutes: verification and a person's choices take their time. */
    for (int ms = 0; ms < 20 * 60 * 1000; ms += 50) {
        state = sceWebBrowserDialogUpdateStatus();
        if (state == 3 || state <= 0) break;
        if (!closing && ms % 1000 == 0 && ms && session_over(token, id)) {
            say("[ORBIT-HELPER] session ended in Orbit; closing the window 0x%08x",
                (unsigned)sceWebBrowserDialogClose());
            closing = true;
        }
        usleep(50000);
    }
    bool matched = false;
    if (state == 3) {
        DialogResult result;
        memset(&result, 0, sizeof result);
        memset(&found, 0, sizeof found);
        found.size = sizeof found;
        found.type = 1;
        result.callback = &found;
        int rc = sceWebBrowserDialogGetResult(&result);
        found.address[sizeof found.address - 1] = 0;
        matched = !rc && result.result == CALLBACK_MATCHED && found.address[0] &&
                  strlen(found.address) < cap;
        if (matched) snprintf(address, cap, "%s", found.address);
        say("[ORBIT-HELPER] window closed 0x%08x result 0x%x matched %d length %zu", (unsigned)rc,
            (unsigned)result.result, matched, strlen(found.address));
        memset(&found, 0, sizeof found);
    }
    sceWebBrowserDialogTerminate();
    return matched;
}

int main(void) {
    static char token[80], status[8192], page[1024], watch[128], window[16], id[48], address[2100], body[4400], escaped[4300];
    int initialized = sceUserServiceInitialize(NULL);
    int user = -1;
    (void)sceUserServiceGetInitialUser(&user);
    /* The service names a page only while it is waiting for this helper. */
    bool helper = request("GET", "/api/v1/session", NULL, NULL, status, sizeof status) == 200 &&
                  field(status, "token", token, sizeof token) && strlen(token) == 64 &&
                  request("GET", "/api/v1/browser", token, NULL, status, sizeof status) == 200 &&
                  field(status, "helperUrl", page, sizeof page) && !strncmp(page, "https://", 8) &&
                  field(status, "id", id, sizeof id) && strlen(id) == 32;
    if (helper && !field(status, "helperWatch", watch, sizeof watch)) watch[0] = 0;
    bool custom = helper && field(status, "helperWindow", window, sizeof window) && !strcmp(window, "custom");
    say("[ORBIT-HELPER] start helper=%d watch=%d custom=%d user=%d", helper, watch[0] != 0, custom, user >= 0);
    if (!helper) {
        open_orbit_in_browser();
    } else if (!run_window(page, watch, custom, user)) {
        snprintf(body, sizeof body, "{\"action\":\"cancel\",\"id\":\"%s\"}", id);
        request("POST", "/api/v1/browser", token, body, status, sizeof status);
    } else {
        snprintf(body, sizeof body, "{\"action\":\"helper-opened\",\"id\":\"%s\"}", id);
        say("[ORBIT-HELPER] opened %d", request("POST", "/api/v1/browser", token, body, status, sizeof status));
        if (wait_window(address, sizeof address, token, id) && quote(address, escaped, sizeof escaped)) {
            snprintf(body, sizeof body, "{\"action\":\"captured\",\"id\":\"%s\",\"url\":\"%s\"}", id, escaped);
            int sent = request("POST", "/api/v1/browser", token, body, status, sizeof status);
            say("[ORBIT-HELPER] captured sent %d", sent);
            if (sent != 202 && sent != 200) {
                snprintf(body, sizeof body, "{\"action\":\"cancel\",\"id\":\"%s\"}", id);
                request("POST", "/api/v1/browser", token, body, status, sizeof status);
            }
        } else {
            snprintf(body, sizeof body, "{\"action\":\"cancel\",\"id\":\"%s\"}", id);
            say("[ORBIT-HELPER] closed without a link %d",
                request("POST", "/api/v1/browser", token, body, status, sizeof status));
        }
        memset(address, 0, sizeof address);
        memset(body, 0, sizeof body);
        memset(escaped, 0, sizeof escaped);
    }
    memset(token, 0, sizeof token);
    if (!initialized) sceUserServiceTerminate();
    usleep(300000);
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
