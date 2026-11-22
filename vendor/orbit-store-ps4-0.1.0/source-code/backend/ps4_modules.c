#include "ps4_modules.h"
#include <sys/mman.h>
#ifndef ORBIT_DESKTOP
#include <ps4/klog.h>
#endif
int ps4_module_private_pages(uintptr_t begin, uintptr_t end, size_t page_size) {
    if (!begin || !page_size || begin % page_size || end % page_size ||
        end <= begin || end - begin > 1024 * 1024) return -1;
    if (mprotect((void *)begin, end - begin, PROT_READ | PROT_WRITE)) return -1;
    /* Force copy-on-write without changing a byte, then restore read-only access.
     * On FW12, another title loading IPMI rewrote the shared relocation page
     * underneath our live client. Touch every page to make its contents private. */
    for (uintptr_t p = begin; p < end; p += page_size) {
        volatile unsigned char *byte = (void *)p;
        *byte = *byte;
    }
    return mprotect((void *)begin, end - begin, PROT_READ);
}
#ifndef ORBIT_DESKTOP
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ps4/kernel.h>
extern int sceKernelLoadStartModule(const char *, size_t, const void *, unsigned, void *, int *);
static pthread_mutex_t module_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t private_handles[32];
static size_t private_count;
static uint32_t isolate_module(uint32_t handle) {
    if (!handle) return 0;
    for (size_t i = 0; i < private_count; i++)
        if (private_handles[i] == handle) return handle;
    if (private_count == sizeof private_handles / sizeof private_handles[0]) return 0;
    dynlib_obj_t module = {0};
    if (kernel_dynlib_obj(-1, handle, &module) ||
        module.textsize > UINTPTR_MAX - module.mapbase) {
        klog_printf("[ORBIT-SVC] module isolation: kernel_dynlib_obj failed handle=0x%08x\n", handle);
        return 0;
    }
    /* Only the non-executable relocation pages between text and writable data.
     * Derive the range from this process's module metadata, not firmware offsets. */
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t begin = module.mapbase + module.textsize;
    if (page_size <= 0 || (begin != module.database &&
        ps4_module_private_pages(begin, module.database, (size_t)page_size))) {
        klog_printf("[ORBIT-SVC] module isolation: relocation pages failed handle=0x%08x\n", handle);
        return 0;
    }
    private_handles[private_count++] = handle;
    return handle;
}
int ps4_modules_initialize(void) {
    static const char *names[] = {"libkernel_sys.sprx", "libkernel_web.sprx", "libkernel.sprx",
        "libSceLibcInternal.sprx", "libSceSysmodule.sprx", "libSceNet.sprx"};
    int result = 0;
    size_t found = 0;
    pthread_mutex_lock(&module_lock);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(-1, names[i], &handle)) {
            klog_printf("[ORBIT-SVC] module initialization: kernel_dynlib_handle failed for %s\n", names[i]);
            continue;
        }
        found++;
        if (!isolate_module(handle)) {
            klog_printf("[ORBIT-SVC] module initialization: isolation failed for %s\n", names[i]);
            result = -1;
            break;
        }
    }
    if (!found) {
        klog_printf("[ORBIT-SVC] module initialization: no system modules could be enumerated\n");
        result = -1;
    }
    pthread_mutex_unlock(&module_lock);
    return result;
}
static uint32_t load(const char *name) {
    uint32_t handle = 0;
    if (strcmp(name, "libSceIpmi.sprx") && !load("libSceIpmi.sprx")) return 0;
    if (!kernel_dynlib_handle(-1, name, &handle))
        return isolate_module(handle);
    if ((!strcmp(name, "libSceUserService.sprx") || !strcmp(name, "libSceSystemService.sprx")) &&
        !load("libSceMbus.sprx")) return 0;
    if (!strcmp(name, "libSceSystemService.sprx") && !load("libSceUserService.sprx")) return 0;
    char path[128];
    if (strchr(name, '/') || snprintf(path, sizeof path, "/system/common/lib/%s", name) >= (int)sizeof path)
        return 0;
    int start = 0;
    int result = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &start);
    if (result < 0 || start < 0) return 0;
    if (kernel_dynlib_handle(-1, name, &handle)) handle = (uint32_t)result;
    return isolate_module(handle);
}
uint32_t ps4_module_load(const char *name) {
    if (!name || !*name) return 0;
    /* Serialize dependency construction across browser, notification and API workers.
     * POSIX dlopen cannot load SystemService in the standalone FW12 payload. */
    pthread_mutex_lock(&module_lock);
    uint32_t handle = load(name);
    pthread_mutex_unlock(&module_lock);
    return handle;
}
void *ps4_module_symbol(uint32_t handle, const char *name) {
    return handle ? (void *)kernel_dynlib_dlsym(getpid(), handle, name) : NULL;
}
#endif
