/* Host name lookups that last. The PS4's own resolver, which the SDK's
 * getaddrinfo uses, aborts the process now and then (PRX_RUNTIME_ERROR inside
 * the network library): a test PS4's service crashed after two hours of use on
 * 13 November 2026, and probes crashed after 5,500 to 13,000 lookups, whether
 * each lookup made its own resolver (the SDK's way), one resolver was kept, or
 * the library's DNS cache was cleared every 256 lookups. A service fetching
 * art, catalogues and downloads makes thousands of lookups in a few hours.
 *
 * So Orbit asks the console's DNS servers itself, over an ordinary UDP socket
 * (the kind curl uses all day), and keeps answers for their time to live, at
 * most five minutes. The servers are the ones in the console's network
 * settings (sceNetGetDnsInfo), so a DNS set up to block Sony's update servers
 * stays in charge. Every address a site has is kept and given to curl, so when
 * one doesn't answer (one of GitHub's four was unreachable from a home network
 * on 19 November 2026) curl moves on to the next instead of every check failing
 * until the five minutes are up. Each lookup starts one address further along,
 * as DNS servers vary the order. Each address goes to the SDK's getaddrinfo as
 * a number, which skips its resolver; ports, checks and the result entries
 * stay the SDK's.
 * Linked with --wrap=getaddrinfo on PS4 only.
 */
#ifndef ORBIT_DESKTOP
#include "dns_message.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int sceNetInit(void);
int sceNetGetDnsInfo(void *info, int flags);
int __real_getaddrinfo(const char *host, const char *service, const struct addrinfo *hints, struct addrinfo **out);

enum { kCacheSize = 64, kLongestKeep = 300, kShortestKeep = 30 };
typedef struct {
    char host[254];
    uint32_t addresses[DNS_MOST_ADDRESSES];
    int count;
    unsigned turn; /* where the next lookup starts */
    int64_t until;
} Kept;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Kept kept[kCacheSize];
static unsigned next_slot;
static bool net_started;

static int64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec;
}

/* The kept addresses for host, starting one further along each time; how many,
 * 0 if none are kept. */
static int cached(const char *host, uint32_t out[DNS_MOST_ADDRESSES]) {
#ifdef ORBIT_DNS_PROBE_NO_KEEP /* private stress test: every lookup asks a server */
    (void)host; (void)out;
    return 0;
#endif
    int count = 0;
    pthread_mutex_lock(&lock);
    int64_t t = now();
    for (int i = 0; i < kCacheSize && !count; ++i)
        if (kept[i].until > t && !strcasecmp(kept[i].host, host)) {
            count = kept[i].count;
            unsigned start = kept[i].turn++;
            for (int k = 0; k < count; ++k) out[k] = kept[i].addresses[(start + (unsigned)k) % (unsigned)count];
        }
    pthread_mutex_unlock(&lock);
    return count;
}

static void keep(const char *host, const uint32_t *addresses, int count, uint32_t ttl) {
    if (strlen(host) >= sizeof kept[0].host) return;
    int64_t seconds = ttl < kShortestKeep ? kShortestKeep : ttl > kLongestKeep ? kLongestKeep : ttl;
    pthread_mutex_lock(&lock);
    Kept *slot = &kept[next_slot++ % kCacheSize];
    for (int i = 0; i < kCacheSize; ++i)
        if (!strcasecmp(kept[i].host, host)) slot = &kept[i];
    snprintf(slot->host, sizeof slot->host, "%s", host);
    memcpy(slot->addresses, addresses, (size_t)count * sizeof *addresses);
    slot->count = count;
    slot->turn = 1; /* this lookup used the reply's order */
    slot->until = now() + seconds;
    pthread_mutex_unlock(&lock);
}

/* The console's DNS servers (network byte order); how many, 0 if unknown. */
static int servers(uint32_t out[2]) {
    pthread_mutex_lock(&lock);
    if (!net_started) net_started = sceNetInit() == 0;
    pthread_mutex_unlock(&lock);
    uint32_t info[64] = {0}; /* the call fills the first two; room to spare */
    if (!net_started || sceNetGetDnsInfo(info, 0) < 0) return 0;
    int count = 0;
    for (int i = 0; i < 2; ++i)
        if (info[i] && info[i] != 0xffffffffu) out[count++] = info[i];
    return count;
}

/* One question to one server: DNS_FOUND, DNS_NO_ADDRESS or DNS_UNUSABLE (no
 * usable reply within the wait). */
static int ask(uint32_t server, const char *host, int wait_ms, uint32_t *addresses, int *count, uint32_t *ttl) {
    uint8_t message[512];
    uint16_t id;
    arc4random_buf(&id, sizeof id);
    int size = dns_query(id, host, message, sizeof message);
    if (size < 0) return DNS_NO_ADDRESS;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return DNS_UNUSABLE;
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_len = sizeof to;
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = server;
    int result = DNS_UNUSABLE;
    if (!connect(fd, (struct sockaddr *)&to, sizeof to) && send(fd, message, (size_t)size, 0) == size) {
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (;;) {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            int left = wait_ms - (int)((t.tv_sec - start.tv_sec) * 1000 + (t.tv_nsec - start.tv_nsec) / 1000000);
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (left <= 0 || poll(&p, 1, left) <= 0) break;
            ssize_t got = recv(fd, message, sizeof message, 0);
            if (got <= 0) break;
            /* A stray or forged packet doesn't match the question; keep waiting. */
            result = dns_answer(message, (size_t)got, id, host, addresses, DNS_MOST_ADDRESSES, count, ttl);
            if (result != DNS_UNUSABLE) break;
        }
    }
    close(fd);
    return result;
}

/* Is host a dotted IPv4 address (the SDK's own resolver would just read it)? */
static bool dotted(const char *host) {
    int parts = 0, digits = 0, value = 0;
    for (const char *c = host;; ++c) {
        if (*c >= '0' && *c <= '9') {
            value = value * 10 + (*c - '0');
            if (++digits > 3 || value > 255) return false;
        } else if ((*c == '.' || !*c) && digits) {
            ++parts;
            digits = value = 0;
            if (!*c) return parts == 4;
        } else {
            return false;
        }
    }
}

int __wrap_getaddrinfo(const char *host, const char *service, const struct addrinfo *hints,
                       struct addrinfo **out) {
    /* The SDK only runs its resolver for a named host with hints asking for
     * IPv4 or any family and none of its interface-list flags. Everything else
     * goes to it unchanged. */
    if (!host || !hints || (hints->ai_flags & AI_NUMERICHOST) || hints->ai_flags >= 0x400 ||
        (hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET))
        return __real_getaddrinfo(host, service, hints, out);
    uint32_t addresses[DNS_MOST_ADDRESSES];
    int count = 0;
    if (dotted(host) || !strcasecmp(host, "localhost") || !strcasecmp(host, "localhost.")) {
        struct in_addr given;
        if (inet_pton(AF_INET, dotted(host) ? host : "127.0.0.1", &given) != 1)
            return __real_getaddrinfo(host, service, hints, out);
        addresses[0] = given.s_addr;
        count = 1;
    } else if (!(count = cached(host, addresses))) {
        uint32_t ttl = 0, list[2];
        int servers_count = servers(list), result = DNS_UNUSABLE;
        /* No servers to ask (not seen on a PS4): the SDK's way, as before. */
        if (!servers_count) return __real_getaddrinfo(host, service, hints, out);
        /* Each server once quickly, then each again with a longer wait. */
        for (int round = 0; round < 2 && result == DNS_UNUSABLE; ++round)
            for (int i = 0; i < servers_count && result == DNS_UNUSABLE; ++i)
                result = ask(list[i], host, round ? 3000 : 1500, addresses, &count, &ttl);
        if (result == DNS_NO_ADDRESS) return EAI_NONAME;
        if (result != DNS_FOUND || count < 1) return EAI_AGAIN;
        keep(host, addresses, count, ttl);
    }
    /* One list with every address, in order: curl tries the next when one doesn't
     * connect. Each part is the SDK's own result for that address as a number, so
     * the SDK's freeaddrinfo frees the joined list entry by entry as usual. */
    struct addrinfo as_number = *hints, *first = NULL, **tail = &first;
    as_number.ai_flags |= AI_NUMERICHOST;
    int rc = EAI_NONAME;
    for (int i = 0; i < count; ++i) {
        const uint8_t *b = (const uint8_t *)&addresses[i];
        char numeric[16];
        snprintf(numeric, sizeof numeric, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        struct addrinfo *part = NULL;
        int got = __real_getaddrinfo(numeric, service, &as_number, &part);
        if (got || !part) {
            if (!first) rc = got ? got : EAI_NONAME;
            continue;
        }
        *tail = part;
        while (*tail) tail = &(*tail)->ai_next;
    }
    if (!first) return rc;
    /* The SDK names the result after the text it was given; keep the host's. */
    if ((hints->ai_flags & AI_CANONNAME) && first->ai_canonname) {
        char *name = strdup(host);
        if (name) {
            free(first->ai_canonname);
            first->ai_canonname = name;
        }
    }
    *out = first;
    return 0;
}
#endif
