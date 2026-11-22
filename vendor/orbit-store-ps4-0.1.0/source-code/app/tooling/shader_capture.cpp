// One-time on-console Piglet shader generation. No compiler modules are bundled.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <orbis/libkernel.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <cstdarg>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "gfx/shader_binary.hpp"
#include "platform/ps4/piglet_config.hpp"
#include "shader_capture_inputs.hpp"

namespace {
constexpr const char *directory = "/download0/orbit-store/shaders";
FILE *log_file = nullptr;
void log(const char *format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (!log_file) printf("%s\n", text);
    if (log_file) { fprintf(log_file, "%s\n", text); fflush(log_file); fsync(fileno(log_file)); }
}

// All graphics entry points come from the loaded compiler-enabled module.
// No Piglet DT_NEEDED import can bind this program to the retail module first.
#define GRAPHICS_FUNCTIONS(X) \
    X(eglGetDisplay) X(eglInitialize) X(eglBindAPI) X(eglChooseConfig) \
    X(eglCreateWindowSurface) X(eglCreateContext) X(eglMakeCurrent) \
    X(eglGetProcAddress) X(eglGetError) X(glGetBooleanv) X(glGetError) \
    X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) \
    X(glGetShaderInfoLog) X(glShaderBinary) X(glDeleteShader) \
    X(glCreateProgram) X(glAttachShader) X(glBindAttribLocation) \
    X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glDeleteProgram)
struct Graphics {
#define DECLARE(name) decltype(&::name) name = nullptr;
    GRAPHICS_FUNCTIONS(DECLARE)
#undef DECLARE
    bool (*configure)(const hui::ps4::ScePglConfig *) = nullptr;
    using Capture = void (*)(GLuint, GLsizei, GLsizei *, GLenum *, void *);
    Capture capture = nullptr;
} api;

int load(const std::string &path)
{
    int start = 0;
    int handle = sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0, nullptr, &start);
    log("load %s: handle=0x%x start=0x%x", path.c_str(), handle, start);
    return handle >= 0 && start >= 0 ? handle : -1;
}

bool connect_compiler(int piglet, int shacc)
{
    // The private 4.74 compiler-enabled pair uses per-process state to record
    // its compiler handle. Bind only the recognized image, following flatz's
    // ps4_gl_test. No firmware, executable code or other process is modified.
    // https://github.com/flatz/ps4_gl_test/blob/master/main.c
    const std::uint8_t fingerprint[20] = {
        0x93,0x3f,0x25,0x0d,0xd6,0x82,0xd0,0xb0,0xb5,0x66,
        0x10,0xbc,0x6d,0x97,0x5d,0x42,0x00,0x00,0x00,0x00};
    const std::uint8_t enabled_check[] = {0x31,0xc0,0xff,0xc0,0x90};
    OrbisKernelModuleInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetModuleInfo(piglet, &info) != 0 ||
        info.segmentCount < 2 || info.segmentCount > 4 ||
        std::memcmp(info.fingerprint, fingerprint, sizeof(fingerprint)) != 0) {
        log("Compiler binding refused: unrecognized Piglet module."); return false;
    }
    auto *base = static_cast<std::uint8_t *>(info.segmentInfo[0].address);
    if (!base || info.segmentInfo[0].size < 0x54524 ||
        std::memcmp(base + 0x5451f, enabled_check, sizeof(enabled_check)) != 0) {
        log("Compiler binding refused: unexpected runtime compiler check."); return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(base) + 0xb2dec;
    const auto end = reinterpret_cast<std::uintptr_t>(base) + 0xb2e28;
    bool writable = false;
    for (unsigned i = 0; i < info.segmentCount; ++i) {
        const auto &segment = info.segmentInfo[i];
        const auto start = reinterpret_cast<std::uintptr_t>(segment.address);
        if ((segment.prot & 2) && begin >= start && end >= begin &&
            end - start <= segment.size) writable = true;
    }
    if (!writable || shacc < 0) {
        log("Compiler binding refused: compiler state is outside writable data."); return false;
    }
    base[0xb2dec] = 0;
    base[0xb2ded] = 0;
    base[0xb2dee] = 1;
    base[0xb2e21] = 1;
    std::memcpy(base + 0xb2e24, &shacc, sizeof(shacc));
    log("Bound the loaded shader compiler to the recognized private Piglet module.");
    return true;
}

bool context()
{
    const char *prefix = nullptr;
    // Select a complete pair from one location; never mix modules from two sets.
    for (const auto *candidate : {"/app0/sce_module/", "/data/self/system/common/lib/"}) {
        struct stat piglet_file{}, shacc_file{};
        const int p = stat((std::string(candidate) + "libScePigletv2VSH.sprx").c_str(), &piglet_file);
        const int pe = errno;
        const int s = stat((std::string(candidate) + "libSceShaccVSH.sprx").c_str(), &shacc_file);
        const int se = errno;
        log("stat %s: Piglet result=%d errno=%d mode=%x size=%lld; Shacc result=%d errno=%d mode=%x size=%lld",
            candidate, p, pe, static_cast<unsigned>(piglet_file.st_mode), static_cast<long long>(piglet_file.st_size),
            s, se, static_cast<unsigned>(shacc_file.st_mode), static_cast<long long>(shacc_file.st_size));
        log("stat ABI: mode=%zu size-offset=%zu total=%zu", sizeof(mode_t), offsetof(struct stat, st_size), sizeof(struct stat));
        if (p == 0 && s == 0 &&
            S_ISREG(piglet_file.st_mode) && S_ISREG(shacc_file.st_mode) &&
            piglet_file.st_size >= 4096 && shacc_file.st_size >= 4096) { prefix = candidate; break; }
    }
    if (!prefix) { log("No complete Piglet/Shacc module pair is available."); return false; }
    const int piglet = load(std::string(prefix) + "libScePigletv2VSH.sprx");
    const int shacc = piglet >= 0 ? load(std::string(prefix) + "libSceShaccVSH.sprx") : -1;
    if (piglet < 0 || shacc < 0) { log("Missing a matching compiler-enabled Piglet/Shacc pair."); return false; }
    if (!connect_compiler(piglet, shacc)) return false;
    const int config_symbol = sceKernelDlsym(piglet, "scePigletSetConfigurationVSH", reinterpret_cast<void **>(&api.configure));
    if (config_symbol != 0 || !api.configure) {
        log("Missing Piglet configuration entry point: result=0x%x available=%d", config_symbol, api.configure != nullptr);
        return false;
    }
#define RESOLVE(name) \
    if (sceKernelDlsym(piglet, #name, reinterpret_cast<void **>(&api.name)) != 0 || !api.name) { \
        log("Missing graphics entry point: %s", #name); return false; }
    GRAPHICS_FUNCTIONS(RESOLVE)
#undef RESOLVE
    auto config = hui::ps4::piglet_configuration(1920, 1080);
    // Compiling Orbit's large fragment stages needs more working memory than
    // loading the finished binaries. This budget belongs only to the private
    // capture utility; the shipped renderer retains its normal allocation.
    config.system_shared_memory_size = 64 * 1024 * 1024;
    if (!api.configure(&config)) { log("Piglet configuration failed."); return false; }
    EGLDisplay display = api.eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
    if (display == EGL_NO_DISPLAY) { log("eglGetDisplay failed: 0x%x", api.eglGetError()); return false; }
    if (!api.eglInitialize(display, &major, &minor)) { log("eglInitialize failed: 0x%x", api.eglGetError()); return false; }
    if (!api.eglBindAPI(EGL_OPENGL_ES_API)) { log("eglBindAPI failed: 0x%x", api.eglGetError()); return false; }
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE};
    EGLConfig chosen = nullptr;
    if (!api.eglChooseConfig(display, attributes, &chosen, 1, &count) || count != 1) {
        log("EGL configuration failed: count=%d error=0x%x", count, api.eglGetError()); return false;
    }
    static int window[4] = {0, 1920, 1080, 0};
    EGLSurface surface = api.eglCreateWindowSurface(display, chosen,
        reinterpret_cast<EGLNativeWindowType>(window), nullptr);
    const EGLint settings[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = api.eglCreateContext(display, chosen, EGL_NO_CONTEXT, settings);
    if (surface == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT ||
        !api.eglMakeCurrent(display, surface, surface, ctx)) {
        log("EGL context failed: 0x%x", api.eglGetError()); return false;
    }
    // Context initialization can reset Piglet's compiler state. Restore the
    // verified in-process binding before compiling the first source stage.
    if (!connect_compiler(piglet, shacc)) return false;
    GLboolean compiler = GL_FALSE;
    api.glGetBooleanv(GL_SHADER_COMPILER, &compiler);
    api.capture = reinterpret_cast<Graphics::Capture>(api.eglGetProcAddress("glPigletGetShaderBinarySCE"));
    log("EGL %d.%d; shader compiler=%d; binary capture=%s", major, minor, compiler, api.capture ? "yes" : "no");
    // This private pair can advertise the retail compiler flag even though its
    // compiler path is present. Actual compile/export/reload/link results below
    // are authoritative; the flag alone is not evidence of a successful capture.
    return api.capture != nullptr;
}

bool capture_program(const CaptureProgram &input)
{
    GLuint shader[2] = {};
    std::vector<std::uint8_t> binaries[2];
    GLenum formats[2] = {};
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) {
        const GLenum stage = i == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
        shader[i] = api.glCreateShader(stage);
        api.glShaderSource(shader[i], 1, &input.sources[i], nullptr);
        api.glCompileShader(shader[i]);
        GLint compiled = GL_FALSE;
        api.glGetShaderiv(shader[i], GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            char error[1536] = {};
            api.glGetShaderInfoLog(shader[i], sizeof(error), nullptr, error);
            log("%s stage %d compile failed: %s", input.name, i, error);
            ok = false; break;
        }
        binaries[i].resize(1024 * 1024 - 4);
        GLsizei size = 0;
        api.capture(shader[i], static_cast<GLsizei>(binaries[i].size()), &size, &formats[i], binaries[i].data());
        if (size <= 0 || static_cast<std::size_t>(size) > binaries[i].size()) { ok = false; break; }
        binaries[i].resize(static_cast<std::size_t>(size));
        // Test the exported bytes, using a fresh shader object, before saving.
        api.glDeleteShader(shader[i]);
        shader[i] = api.glCreateShader(stage);
        for (int errors = 0; errors < 16 && api.glGetError() != GL_NO_ERROR; ++errors) {}
        api.glShaderBinary(1, &shader[i], formats[i], binaries[i].data(), size);
        api.glGetShaderiv(shader[i], GL_COMPILE_STATUS, &compiled);
        ok = compiled == GL_TRUE && api.glGetError() == GL_NO_ERROR;
        log("%s stage %d: %d bytes, format=0x%x, reloaded=%s", input.name, i, size, formats[i], ok ? "yes" : "no");
    }
    if (ok) {
        GLuint program = api.glCreateProgram();
        api.glAttachShader(program, shader[0]); api.glAttachShader(program, shader[1]);
        for (GLuint i = 0; input.attributes[i]; ++i) api.glBindAttribLocation(program, i, input.attributes[i]);
        api.glLinkProgram(program);
        GLint linked = GL_FALSE;
        api.glGetProgramiv(program, GL_LINK_STATUS, &linked);
        ok = linked == GL_TRUE;
        if (!ok) {
            char error[1536] = {};
            api.glGetProgramInfoLog(program, sizeof(error), nullptr, error);
            log("%s exported pair failed to link: %s", input.name, error);
        }
        api.glDeleteProgram(program);
    }
    if (ok) for (int i = 0; i < 2; ++i) {
        const std::string base = std::string(directory) + '/' + input.name + (i == 0 ? ".vert" : ".frag");
        if (!hui::gfx::write_shader_binary(base, input.sources[i], formats[i], binaries[i].data(), binaries[i].size())) ok = false;
    }
    for (auto id : shader) if (id) api.glDeleteShader(id);
    log("%s: %s", input.name, ok ? "saved" : "FAILED");
    return ok;
}

void show_result(bool success)
{
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    const std::string module_path = std::string("/") + (sandbox ? sandbox : "") + "/common/lib/";
    // VSH-authorized applications do not receive the game's preloaded services.
    // CommonDialog imports UserService, so load it before opening the result.
    if (load(module_path + "libSceUserService.sprx") < 0) return;
    const int system = load(module_path + "libSceSystemService.sprx");
    int (*hide_splash)() = nullptr;
    if (system >= 0 && sceKernelDlsym(system, "sceSystemServiceHideSplashScreen",
            reinterpret_cast<void **>(&hide_splash)) == 0 && hide_splash)
        hide_splash();
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) != 0 ||
        sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG) != 0) return;
    const int common = sceCommonDialogInitialize();
    if (common != 0 && static_cast<unsigned>(common) != 0x80b80002u) return;
    if (sceMsgDialogInitialize() != 0) return;
    OrbisMsgDialogUserMessageParam message{};
    message.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    message.msg = success ? "Orbit: all eight shaders compiled, reloaded and linked. Copy the files and capture.log from /download0/orbit-store/shaders while this app is open."
        : "Orbit shader capture failed. Check capture.log in /download0/orbit-store/shaders. A matching compiler-enabled Piglet and Shacc module pair is required.";
    OrbisMsgDialogParam param{};
    param.baseParam.size = sizeof(param.baseParam);
    param.baseParam.magic = 0xc0d1a109u + static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&param.baseParam));
    param.size = sizeof(param); param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG; param.userMsgParam = &message;
    if (sceMsgDialogOpen(&param) == 0) {
        for (int i = 0; i < 36000; ++i) {
            int state = sceMsgDialogUpdateStatus();
            if (state <= 0 || state == 3) break;
            sceKernelUsleep(100000);
        }
        sceMsgDialogClose();
    }
    sceMsgDialogTerminate();
}
} // namespace

int main()
{
    mkdir("/download0/orbit-store", 0755); mkdir(directory, 0755);
    log_file = fopen((std::string(directory) + "/capture.log").c_str(), "w");
    if (log_file) {
        dup2(fileno(log_file), STDOUT_FILENO);
        dup2(fileno(log_file), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    // A failed new attempt must not leave a previous successful capture behind.
    for (const auto &input : programs) for (const auto *stage : {".vert", ".frag"}) {
        const std::string base = std::string(directory) + '/' + input.name + stage;
        unlink((base + ".glsl").c_str()); unlink((base + ".bin").c_str());
    }
    bool success = log_file && context();
    if (success) for (const auto &input : programs) if (!capture_program(input)) success = false;
    log("Capture result: %s", success ? "ALL EIGHT STAGES PASSED" : "FAILED; do not import partial output");
    show_result(success);
    // Keep the sandbox available for readback, even after the result is closed.
    // Closing this diagnostic through the shell is explicit and leaves the
    // console's other applications untouched.
    if (log_file) { fclose(log_file); log_file = nullptr; }
    for (;;) sceKernelUsleep(100000);
}
