#include "dns_message.h"
#include <string.h>

enum { kHeader = 12, kTypeA = 1, kTypeCname = 5, kClassIn = 1, kMaxName = 253 };

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* host in lower case without one trailing dot; false when it can't be sent. */
static int plain_host(const char *host, char out[kMaxName + 2]) {
    size_t n = host ? strlen(host) : 0;
    if (n && host[n - 1] == '.') n--;
    if (!n || n > kMaxName) return 0;
    size_t label = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c <= ' ' || c >= 0x7f) return 0;
        if (c == '.') {
            if (!label) return 0;
            label = 0;
        } else if (++label > 63) {
            return 0;
        }
        out[i] = lower((char)c);
    }
    if (!label) return 0;
    out[n] = 0;
    return 1;
}

int dns_query(uint16_t id, const char *host, uint8_t *out, size_t cap) {
    char name[kMaxName + 2];
    if (!plain_host(host, name)) return -1;
    size_t n = strlen(name), need = kHeader + n + 2 + 4;
    if (cap < need) return -1;
    memset(out, 0, kHeader);
    out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id;
    out[2] = 0x01; /* recursion desired */
    out[5] = 1;    /* one question */
    uint8_t *p = out + kHeader;
    for (const char *label = name; *label;) {
        const char *dot = strchr(label, '.');
        size_t len = dot ? (size_t)(dot - label) : strlen(label);
        *p++ = (uint8_t)len;
        memcpy(p, label, len);
        p += len;
        label += len + (dot ? 1 : 0);
    }
    *p++ = 0;
    *p++ = 0; *p++ = kTypeA; *p++ = 0; *p++ = kClassIn;
    return (int)(p - out);
}

/* The name at *at (following compression pointers) as lower-case dotted text;
 * *at moves past it. 0, or -1 when malformed. */
static int read_name(const uint8_t *m, size_t length, size_t *at, char out[256]) {
    size_t pos = *at, n = 0;
    int jumps = 0;
    for (;;) {
        if (pos >= length) return -1;
        uint8_t len = m[pos];
        if ((len & 0xc0) == 0xc0) {
            if (pos + 1 >= length || ++jumps > 32) return -1;
            if (jumps == 1) *at = pos + 2; /* the name ends at its first pointer */
            pos = (size_t)(len & 0x3f) << 8 | m[pos + 1];
            continue;
        }
        if (len & 0xc0) return -1;
        if (!len) {
            if (!jumps) *at = pos + 1;
            out[n] = 0;
            return 0;
        }
        if (pos + 1 + len > length || n + len + 1 > 255) return -1;
        if (n) out[n++] = '.';
        for (size_t i = 0; i < len; ++i) out[n++] = lower((char)m[pos + 1 + i]);
        pos += 1 + len;
    }
}

int dns_answer(const uint8_t *reply, size_t length, uint16_t id, const char *host,
               uint32_t *addresses, int cap, int *count, uint32_t *ttl) {
    char want[kMaxName + 2], name[256];
    *count = 0;
    if (!reply || length < kHeader || !plain_host(host, want) || get16(reply) != id || cap < 1) return DNS_UNUSABLE;
    uint16_t flags = get16(reply + 2);
    if (!(flags & 0x8000) || (flags & 0x7800)) return DNS_UNUSABLE; /* not an answer, or not a query */
    int rcode = flags & 0x0f, truncated = (flags & 0x0200) != 0;
    if (get16(reply + 4) != 1) return DNS_UNUSABLE;
    size_t at = kHeader;
    if (read_name(reply, length, &at, name) || strcmp(name, want) || at + 4 > length ||
        get16(reply + at) != kTypeA || get16(reply + at + 2) != kClassIn)
        return DNS_UNUSABLE;
    at += 4;
    if (rcode == 3) return DNS_NO_ADDRESS; /* no such name */
    if (rcode) return DNS_UNUSABLE;
    /* Answers: walk the CNAME chain from the host to the name holding its A
     * records. The records are read again for each step, so their order
     * doesn't matter. */
    size_t answers = at;
    unsigned records = get16(reply + 6);
    char current[256];
    memcpy(current, want, strlen(want) + 1);
    for (int step = 0; step < 16; ++step) {
        int moved = 0, found = 0;
        uint32_t shortest = 0;
        char next[256];
        at = answers;
        for (unsigned i = 0; i < records; ++i) {
            if (read_name(reply, length, &at, name) || at + 10 > length) return DNS_UNUSABLE;
            uint16_t type = get16(reply + at), klass = get16(reply + at + 2);
            uint32_t seconds = get32(reply + at + 4);
            size_t size = get16(reply + at + 8), data = at + 10;
            if (data + size > length) return DNS_UNUSABLE;
            at = data + size;
            if (klass != kClassIn || strcmp(name, current)) continue;
            if (type == kTypeA && size == 4) {
                uint32_t address;
                memcpy(&address, reply + data, 4);
                seconds = seconds & 0x80000000u ? 0 : seconds;
                if (!found || seconds < shortest) shortest = seconds;
                found = 1;
                int seen = 0;
                for (int k = 0; k < *count; ++k) seen |= addresses[k] == address;
                if (!seen && *count < cap) addresses[(*count)++] = address;
            } else if (type == kTypeCname && !moved) {
                size_t target = data;
                if (read_name(reply, length, &target, next) || target != data + size) return DNS_UNUSABLE;
                moved = 1;
            }
        }
        if (found) {
            *ttl = shortest;
            return DNS_FOUND;
        }
        if (!moved) break;
        memcpy(current, next, strlen(next) + 1);
    }
    return truncated ? DNS_UNUSABLE : DNS_NO_ADDRESS;
}
