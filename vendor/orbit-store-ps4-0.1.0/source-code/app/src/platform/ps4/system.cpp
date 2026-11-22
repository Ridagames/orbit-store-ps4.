// ps4-homebrew-ui - PS4 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps4/system.hpp"
#ifdef ORBIT_PERF_TRACE
#include "orbit/perf_probe.hpp"
#endif

#include <atomic>
#include <deque>
#include <string>
#include <cstdarg>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

extern "C"
{
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
}

extern "C" void hui_release_splash(void) __attribute__((weak));

namespace hui::sys
{
namespace
{
FILE *native_log = nullptr;
#ifdef ORBIT_PERF_TRACE
pthread_t perf_main_thread = pthread_self(); // set again in initialize_logging
#endif
std::size_t native_log_bytes = 0;
constexpr std::size_t kLogLimit = 128 * 1024;

bool isolate_module_data(int handle)
{
    OrbisKernelModuleInfo module{};
    module.size = sizeof(module);
    if (sceKernelGetModuleInfo(handle, &module) != 0 || module.segmentCount > 4) return false;
    // FW12's shared relocation pages changed when a second Orbit process loaded
    // the same library. Fault in private copies before another app can relocate it.
    constexpr std::uintptr_t page = 0x4000;
    for (std::uint32_t i = 0; i < module.segmentCount; ++i)
    {
        const auto &segment = module.segmentInfo[i];
        if (segment.prot != PROT_READ || segment.size == 0) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(segment.address);
        if (begin % page || segment.size % page || segment.size > 8 * 1024 * 1024 ||
            begin > UINTPTR_MAX - segment.size) return false;
        if (::mprotect(segment.address, segment.size, PROT_READ | PROT_WRITE) != 0) return false;
        for (std::uintptr_t at = begin; at < begin + segment.size; at += page)
        {
            auto *byte = reinterpret_cast<volatile unsigned char *>(at);
            *byte = *byte;
        }
        if (::mprotect(segment.address, segment.size, PROT_READ) != 0) return false;
    }
    return true;
}
}

void initialize_logging()
{
    // Start once, on the main thread, before workers. Keep the latest run small
    // and readable through FTP even when the system log reader cannot run.
    if (native_log) return;
#ifdef ORBIT_PERF_TRACE
    perf_main_thread = pthread_self();
#endif
    (void)::mkdir("/download0/orbit-store", 0700);
    const int fd = ::open("/download0/orbit-store/native-startup.log",
                          O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return;
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        ::ftruncate(fd, 0) != 0)
    {
        ::close(fd);
        return;
    }
    native_log = ::fdopen(fd, "w");
    if (!native_log) ::close(fd);
}

bool initialize_platform()
{
    OrbisKernelModule handles[128]{};
    std::size_t count = 0;
    if (sceKernelGetModuleList(handles, 128, &count) != 0 || count > 128) return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        OrbisKernelModuleInfo info{};
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[i], &info) != 0) return false;
        if (std::strncmp(info.name, "libkernel", sizeof("libkernel") - 1) == 0 ||
            std::strncmp(info.name, "libSceLibcInternal", sizeof("libSceLibcInternal") - 1) == 0 ||
            std::strncmp(info.name, "libSceSysmodule", sizeof("libSceSysmodule") - 1) == 0 ||
            std::strncmp(info.name, "libSceNet", sizeof("libSceNet") - 1) == 0)
        {
            const bool isolated = isolate_module_data(handles[i]);
            log("[ORBIT] initial module %.255s private-data=%d", info.name, isolated);
            if (!isolated) return false;
        }
    }
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    if (!sandbox || !*sandbox) return false;
    // A system-authorized app does not inherit the dependencies loaded for a game.
    // Pad uses Mbus; SystemService and the installer use IPMI.
    for (const char *module : {"libSceIpmi.sprx", "libSceMbus.sprx",
                              "libSceUserService.sprx", "libSceSystemService.sprx",
                              "libScePad.sprx", "libSceAudioOut.sprx",
                              "libScePigletv2VSH.sprx"})
    {
        char path[256];
        const int length = std::snprintf(path, sizeof(path), "/%s/common/lib/%s", sandbox, module);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(path)) return false;
        int start = 0;
        log("[ORBIT] loading system module %s", module);
        const int handle = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, &start);
        log("[ORBIT] system module %s handle=0x%x start=0x%x", module, handle, start);
        if (handle < 0 || start < 0) return false;
        const bool isolated = isolate_module_data(handle);
        log("[ORBIT] system module %s private-data=%d", module, isolated);
        if (!isolated) return false;
    }
    return true;
}

std::int64_t monotonic_us()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

namespace
{
// One line to the native log, stdout and klog, in that order.
void write_line(const char *line, std::size_t used, bool sync)
{
    if (native_log)
    {
        ::flockfile(native_log);
        if (used + 1 <= kLogLimit - native_log_bytes)
        {
            native_log_bytes += used + 1;
            std::fputs(line, native_log);
            std::fflush(native_log);
            if (sync)
                (void)::fsync(::fileno(native_log));
        }
        ::funlockfile(native_log);
    }
    std::fputs(line, stdout);
    // Firmware writes this text literally despite the SDK's variadic declaration.
    sceKernelDebugOutText(0, line);
}

// After start-up a thread writes the lines: on the PS4's hard drive one line
// held its caller up to 200 ms (fsync) and up to 100 ms without it, and the
// frame loop logs too. Lines keep their order; past kQueueLimit waiting lines
// the rest are counted and dropped rather than held in memory.
constexpr std::size_t kQueueLimit = 256;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_ready = PTHREAD_COND_INITIALIZER;
std::deque<std::string> queued_lines;
std::size_t dropped_lines = 0;
std::atomic<bool> writer_running{false};

// Writes what is waiting; true when there was something.
bool write_queued()
{
    std::deque<std::string> lines;
    std::size_t dropped = 0;
    pthread_mutex_lock(&queue_mutex);
    lines.swap(queued_lines);
    std::swap(dropped, dropped_lines);
    pthread_mutex_unlock(&queue_mutex);
    for (const std::string &line : lines)
        write_line(line.c_str(), line.size() - 1, false);
    if (dropped > 0)
    {
        char note[96];
        const int length = std::snprintf(note, sizeof(note), "[HUI] %zu log lines dropped\n", dropped);
        if (length > 0)
            write_line(note, static_cast<std::size_t>(length) - 1, false);
    }
    return !lines.empty() || dropped > 0;
}

void *log_writer(void *)
{
    for (;;)
    {
        pthread_mutex_lock(&queue_mutex);
        while (queued_lines.empty() && dropped_lines == 0)
            pthread_cond_wait(&queue_ready, &queue_mutex);
        pthread_mutex_unlock(&queue_mutex);
        (void)write_queued();
    }
    return nullptr;
}
} // namespace

void log(const char *format, ...)
{
    // klog lines are short; longer messages are truncated rather than split.
    char line[384];
    va_list arguments;
    va_start(arguments, format);
    int length = std::vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    std::size_t used = std::strlen(line);
    line[used] = '\n';
    line[used + 1] = '\0';
#ifdef ORBIT_PERF_TRACE
    // Test-only: the frame loop's time in here.
    const bool perf_main = pthread_equal(pthread_self(), perf_main_thread) != 0;
    const std::int64_t perf_start = monotonic_us();
#endif
    if (!writer_running.load(std::memory_order_acquire))
        write_line(line, used, true); // still starting: every line on the disk before the next
    else
    {
        pthread_mutex_lock(&queue_mutex);
        if (queued_lines.size() < kQueueLimit)
            queued_lines.emplace_back(line, used + 1);
        else
            ++dropped_lines;
        pthread_cond_signal(&queue_ready);
        pthread_mutex_unlock(&queue_mutex);
    }
#ifdef ORBIT_PERF_TRACE
    if (perf_main)
        orbit::perf::slot_us[orbit::perf::log_write] += monotonic_us() - perf_start;
#endif
}

void log_started()
{
    if (writer_running.load(std::memory_order_acquire))
        return;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 256 * 1024);
    pthread_t thread;
    const int started = pthread_create(&thread, &attributes, log_writer, nullptr);
    pthread_attr_destroy(&attributes);
    if (started != 0)
    {
        log("[HUI] log writer not started; logging stays on the caller");
        return;
    }
    pthread_detach(thread);
    writer_running.store(true, std::memory_order_release);
}

bool hide_splash_screen()
{
    // The runtime holds earlier requests back (runtime_shims.c): this is the
    // one that counts. An app with a runtime of its own has no such hold.
    if (hui_release_splash)
        hui_release_splash();
    return sceSystemServiceHideSplashScreen() == 0;
}

void sleep_us(std::uint32_t microseconds)
{
    sceKernelUsleep(microseconds);
}

void park()
{
    while (write_queued())
    {
    }
    std::fflush(nullptr);
    for (;;)
        sceKernelUsleep(100000);
}

void quit()
{
    log("[HUI] quit requested");
    std::fflush(nullptr);
    const int refused = sceSystemServiceLoadExec("exit", nullptr);
    log("[HUI] quit refused rc=0x%x", static_cast<unsigned>(refused));
    park();
}

void startup_failure(const char *reason)
{
    log("[ORBIT] startup failed: %s", reason);
    hide_splash_screen();
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) == 0 &&
        sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG) == 0)
    {
        const int common = sceCommonDialogInitialize();
        if ((common == 0 || static_cast<std::uint32_t>(common) == 0x80b80002u) &&
            sceMsgDialogInitialize() == 0)
        {
            OrbisMsgDialogUserMessageParam message{};
            message.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
            message.msg = "Orbit could not open its TV interface. Reinstall the complete Orbit Store TV package, or use Orbit in the Internet Browser.";
            OrbisMsgDialogParam param{};
            param.baseParam.size = sizeof(param.baseParam);
            param.baseParam.magic = 0xc0d1a109u + static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(&param.baseParam));
            param.size = sizeof(param);
            param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
            param.userMsgParam = &message;
            if (sceMsgDialogOpen(&param) == 0)
            {
                const auto deadline = monotonic_us() + 60000000;
                while (monotonic_us() < deadline)
                {
                    const int state = sceMsgDialogUpdateStatus();
                    if (state <= 0 || state == 3) break;
                    sleep_us(100000);
                }
                sceMsgDialogClose();
            }
            sceMsgDialogTerminate();
        }
    }
    quit();
}

} // namespace hui::sys
