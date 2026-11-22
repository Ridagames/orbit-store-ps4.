/* The PS4 libc's strerror() raises a fatal PRX_RUNTIME_ERROR (0xa0020103) for
 * any code above 94, the last FreeBSD error it has text for (measured on a PS4
 * on firmware 12.00: 94 works, 95, 96, 163 and 1000 end the process). Sony's
 * own network codes are higher: when the PS4's network dropped, accept() failed
 * with 163 and libmicrohttpd, which passes strerror() to its accept-error
 * message even when logging is off, ended the service. strerror_r() is safe
 * for every code. Linked with --wrap=strerror on PS4 only; desktop builds use
 * the host libc. ORBIT_STRERROR_TEST builds it on the host for its test.
 */
#if !defined(ORBIT_DESKTOP) || defined(ORBIT_STRERROR_TEST)
#include <stdatomic.h>
#include <stdio.h>

enum { PS4_LAST_LIBC_ERROR = 94 };

char *__real_strerror(int code);

char *__wrap_strerror(int code) {
    if (code >= 1 && code <= PS4_LAST_LIBC_ERROR) return __real_strerror(code);
    /* Like strerror(), the text is valid until a later call: a few rotating
     * buffers keep concurrent callers from overwriting each other at once. */
    static char text[8][32];
    static atomic_uint next;
    char *slot = text[atomic_fetch_add_explicit(&next, 1, memory_order_relaxed) % 8];
    if (code == 0) snprintf(slot, sizeof text[0], "No error");
    else snprintf(slot, sizeof text[0], "Unknown error %d", code);
    return slot;
}
#endif
