/* The SDK's raw *at stubs leave FreeBSD errors in rax with carry set.
 * Normalize them to POSIX -1/errno before the application sees a descriptor.
 * Linked with --wrap on PS4 only; desktop builds use the host libc.
 */
#ifndef ORBIT_DESKTOP
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static long filesystem_call(long number, long a, long b, long c, long d, long e) {
    register long fourth __asm__("r10") = d;
    register long fifth __asm__("r8") = e;
    unsigned char failed;
    __asm__ volatile("syscall\n\tsetc %1"
                     : "+a"(number), "=qm"(failed)
                     : "D"(a), "S"(b), "d"(c), "r"(fourth), "r"(fifth)
                     : "rcx", "r11", "cc", "memory");
    if (failed) {
        errno = (int)number;
        return -1;
    }
    return number;
}

int __wrap_openat(int dir, const char *path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return (int)filesystem_call(499, dir, (intptr_t)path, flags, mode, 0);
}

int __wrap_mkdirat(int dir, const char *path, mode_t mode) {
    return (int)filesystem_call(496, dir, (intptr_t)path, mode, 0, 0);
}

int __wrap_fstatat(int dir, const char *path, struct stat *st, int flags) {
    return (int)filesystem_call(493, dir, (intptr_t)path, (intptr_t)st, flags, 0);
}

int __wrap_linkat(int from, const char *source, int to, const char *target, int flags) {
    return (int)filesystem_call(495, from, (intptr_t)source, to, (intptr_t)target, flags);
}

int __wrap_renameat(int from, const char *source, int to, const char *target) {
    return (int)filesystem_call(501, from, (intptr_t)source, to, (intptr_t)target, 0);
}

int __wrap_unlinkat(int dir, const char *path, int flags) {
    return (int)filesystem_call(503, dir, (intptr_t)path, flags, 0, 0);
}

/* Orbis predates futimens: FreeBSD 9 offers futimes(2) instead. Convert the
 * nanosecond pair and set timestamps through the opened descriptor, without
 * resolving the destination again. Callers pass concrete atime/mtime only. */
int __wrap_futimens(int fd, const struct timespec times[2]) {
    struct timeval stamp[2] = {{times[0].tv_sec, times[0].tv_nsec / 1000},
                               {times[1].tv_sec, times[1].tv_nsec / 1000}};
    return (int)filesystem_call(206, fd, (intptr_t)stamp, 0, 0, 0);
}

/* UnRAR links these for what Orbit never asks of it: restoring owners (its -ow
 * option) and a symlink's times (backend/unrar.cpp refuses links before writing).
 * The PS4 libc has neither. */
struct group;
struct group *getgrnam(const char *name) {
    (void)name;
    errno = ENOENT;
    return 0;
}
int lutimes(const char *path, const struct timeval *times) {
    (void)path; (void)times;
    errno = ENOSYS;
    return -1;
}
#endif
