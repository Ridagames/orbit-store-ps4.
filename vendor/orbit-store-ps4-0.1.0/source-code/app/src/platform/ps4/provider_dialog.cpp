// ps4-homebrew-ui - Provider sign-in through the system web browser dialog.
// SPDX-License-Identifier: GPL-3.0-or-later
// Lifecycle ported from the PS5 bring-up; PS4 parameter ABI is provisional
// (see provider_dialog_abi.hpp) pending console verification.
#include "platform/ps4/provider_dialog.hpp"
#include "platform/ps4/provider_dialog_abi.hpp"
#include "platform/ps4/system.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>

extern "C" {
int sceSysmoduleLoadModule(std::uint16_t);
int sceSysmoduleLoadModuleInternal(std::uint32_t);
// Resolve through native module imports at load time. The PS4 runtime
// behavior still needs console verification.
int sceCommonDialogInitialize(void);
int sceWebBrowserDialogInitialize(void);
int sceWebBrowserDialogOpen(hui::ps4::dialog_abi::Param *);
int sceWebBrowserDialogUpdateStatus(void);
int sceWebBrowserDialogGetResult(hui::ps4::dialog_abi::Result *);
int sceWebBrowserDialogClose(void);
int sceUserServiceGetForegroundUser(int *);
int sceUserServiceGetInitialUser(int *);
}
namespace {
using hui::ps4::dialog_abi::Base;
using hui::ps4::dialog_abi::Param;
using hui::ps4::dialog_abi::Result;
alignas(16) Param param;
std::string page;

// OpenOrbis include/orbis/_types/sysmodule.h: CommonDialog is an INTERNAL
// (32-bit) module. The PS5 IDs here would load TextToSpeech and PlayGo on PS4.
constexpr std::uint32_t kModuleCommonDialog = 0x80000018;
constexpr std::uint16_t kModuleWebBrowserDialog = 0x00ab;
constexpr std::uint32_t kAlreadyInitialized = 0x80b80002u;

// Bounded, best-effort lifecycle diagnostics. Never record the page URL,
// signed file links, cookies, credentials, or browser content.
void trace(const char *phase, int result) {
    hui::sys::log("[ORBIT] provider-dialog %s rc=0x%x", phase, static_cast<unsigned>(result));
    // Native titles cannot write the service's /data directory. Use the
    // same persistent title mount as appearance settings and runtime logs.
    (void)::mkdir("/download0/orbit-store", 0755);
    constexpr const char *path = "/download0/orbit-store/native-provider.log";
    std::FILE *file = std::fopen(path, "a+");
    if (!file) return;
    if (std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) > 16384) {
        std::fclose(file); file = std::fopen(path, "w");
    }
    if (!file) return;
    std::fprintf(file, "%lld %s 0x%x\n", static_cast<long long>(hui::sys::monotonic_us()), phase, static_cast<unsigned>(result));
    std::fclose(file);
}

} // namespace
namespace hui::ps4 {
bool ProviderDialog::prepare() {
    if (ready_) return true;
    error_code_ = 0;
    const int load_common = sceSysmoduleLoadModuleInternal(kModuleCommonDialog);
    trace("module-common", load_common);
    if (load_common != 0) { error_code_ = load_common; return false; }
    const int load_browser = sceSysmoduleLoadModule(kModuleWebBrowserDialog);
    trace("module-browser", load_browser);
    if (load_browser != 0) { error_code_ = load_browser; return false; }
    const int common_rc = sceCommonDialogInitialize();
    trace("common-init", common_rc);
    if (common_rc != 0 && static_cast<std::uint32_t>(common_rc) != kAlreadyInitialized) {
        error_code_ = common_rc;
        return false;
    }
    const int rc = sceWebBrowserDialogInitialize();
    trace("init", rc);
    ready_ = rc == 0;
    if (!ready_) error_code_ = rc;
    // Keep it initialized for this process lifetime, including between opens.
    return ready_;
}
bool ProviderDialog::open(const std::string &url) {
    if (owned_ || url.rfind("https://", 0) != 0 || !prepare()) return false;
    error_code_ = 0;
    int user = -1;
    if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
        if (sceUserServiceGetInitialUser(&user) != 0 || user < 0) return false;
    page = url;
    param = {};
    param.base.size = sizeof(Base);
    // The CommonDialog base magic is derived from the param address on both
    // consoles; the ABI layout stays PROVISIONAL until verified on hardware.
    param.base.magic = 0xc0d1a109u + static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&param.base));
    param.size = sizeof(param); param.mode = 1; param.user = user; param.url = page.c_str();
    const int rc = sceWebBrowserDialogOpen(&param);
    if (rc < 0) error_code_ = rc;
    trace("open", rc);
    owned_ = rc == 0; last_status_ = -1; running_ = requested_close_ = false;
    opened_at_ = sys::monotonic_us();
    if (!owned_) page.clear();
    return owned_;
}
int ProviderDialog::status() {
    if (!owned_) return 0;
    int state = sceWebBrowserDialogUpdateStatus();
    if (state < 0) error_code_ = state;
    if (state == 2) running_ = true;
    const bool stopped = state == 1 && (running_ || sys::monotonic_us() - opened_at_ > 5000000);
    if (state != last_status_) trace("status", state);
    last_status_ = state;
    if (state <= 0 || state == 3 || stopped) {
        if (state == 3) {
            Result result{};
            const int rc = sceWebBrowserDialogGetResult(&result);
            trace("finished", rc); trace("result", result.result);
            // A positive result can be a normal user dismissal. Do not invent
            // a network diagnosis from undocumented result values.
            if (rc < 0) error_code_ = rc;
            else if (rc == 0 && result.result < 0) error_code_ = result.result;
        }
        if (!requested_close_) (void)sceWebBrowserDialogClose();
        owned_ = false; page.clear();
    }
    return state;
}
int ProviderDialog::close() {
    if (!owned_) return -1;
    const int rc = sceWebBrowserDialogClose();
    requested_close_ = rc == 0;
    trace("close", rc);
    return rc;
}
} // namespace hui::ps4
