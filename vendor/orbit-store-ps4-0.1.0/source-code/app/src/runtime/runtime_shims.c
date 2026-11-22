// ps4-homebrew-ui - Process-level runtime shims for the native title.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app log receipt and the splash-release gate. Everything else the PS5
// clean-room runtime provided (malloc routing, mkstemp, isatty, assert,
// popen stubs, Mesa TLS) is covered by the OpenOrbis musl libc on PS4.

#include <stdio.h>
#include <sys/stat.h>

extern int sceKernelUsleep(unsigned int microseconds);

/* The log goes into the title's own storage (a sandbox has no /data). While
 * the title runs, FTP reads it at
 * /mnt/sandbox/<TITLE_ID>_000/download0/hui/dev/app.log. */
#define HUI_PARENT_DIR "/download0/hui"
#define HUI_DATA_DIR HUI_PARENT_DIR "/dev"
#define HUI_LOG_PATH HUI_DATA_DIR "/app.log"

__attribute__((constructor)) static void hui_open_log(void)
{
    mkdir(HUI_PARENT_DIR, 0755);
    mkdir(HUI_DATA_DIR, 0755);
    /* Keep the previous launch's log for post-close inspection. */
    rename(HUI_LOG_PATH, HUI_DATA_DIR "/app.prev.log");
    FILE *stream = freopen(HUI_LOG_PATH, "w", stdout);
    /* Start a fresh receipt, then make both streams append-only and unbuffered
     * so the log survives a shell close or a GPU fail-stop. */
    if (stream != NULL)
        stream = freopen(HUI_LOG_PATH, "a", stdout);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
    stream = freopen(HUI_LOG_PATH, "a", stderr);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the shell closes the title. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[HUI] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

/* The console's splash picture stays up until the app has presented its first
 * frame. The display asks to hide it as soon as the EGL surface opens, which
 * leaves a black screen while programs build and fonts load (several seconds
 * at 1080p). The build routes every such call here (--wrap), and
 * sys::hide_splash_screen() lets them through. */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int hui_splash_released;

void hui_release_splash(void)
{
    hui_splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return hui_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}
