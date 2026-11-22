#include "goldhen_autorun.h"
#include <stdlib.h>
#include <string.h>

static bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool equal(const char *s, size_t n, const char *text) {
    return strlen(text) == n && !memcmp(s, text, n);
}
/* 1 section header, 2 Orbit entry in AutoRun, otherwise untouched text. */
static int line(const char *s, size_t n, bool *section, bool *enabled) {
    while (n && space(*s)) { ++s; --n; }
    while (n && space(s[n - 1])) --n;
    if (n && *s == '[') {
        const char *end = memchr(s, ']', n);
        if (end) { *section = equal(s + 1, (size_t)(end - s - 1), "AutoRun"); return 1; }
    }
    if (!*section || !n || *s == ';' || *s == '#') return 0;
    const char *sep = memchr(s, '=', n);
    if (!sep) return 0;
    size_t key = (size_t)(sep - s);
    while (key && space(s[key - 1])) --key;
    if (!equal(s, key, "/user/data/payloads/orbit_store.elf") &&
        !equal(s, key, "/data/payloads/orbit_store.elf")) return 0;
    const char *value = sep + 1, *end = s + n;
    while (value < end && space(*value)) ++value;
    *enabled = value < end && *value++ == '1';
    while (value < end && space(*value)) ++value;
    if (value < end && *value != ';' && *value != '#') *enabled = false;
    return 2;
}
bool goldhen_autorun_enabled(const char *s, size_t size) {
    if (!s || memchr(s, 0, size)) return false;
    bool section = false, enabled = false;
    for (size_t i = 0; i < size;) {
        const char *end = memchr(s + i, '\n', size - i);
        size_t n = end ? (size_t)(end - s - i) : size - i;
        line(s + i, n, &section, &enabled);
        i += n + (end != NULL);
    }
    return enabled;
}
int goldhen_autorun_edit(const char *s, size_t size, bool enable, char **out, size_t *length) {
    *out = NULL; *length = 0;
    if (!s || size > 1024 * 1024 || memchr(s, 0, size)) return -1;
    char *buffer = malloc(size + 128);
    if (!buffer) return -1;
    size_t used = 0;
    bool section = false, enabled = false, inserted = false;
    const char *entry = "/user/data/payloads/orbit_store.elf = 1\n";
    for (size_t i = 0; i < size;) {
        const char *end = memchr(s + i, '\n', size - i);
        size_t n = end ? (size_t)(end - s - i) : size - i;
        bool was_section = section;
        int type = line(s + i, n, &section, &enabled);
        if (enable && !inserted && was_section && type == 1) {
            memcpy(buffer + used, entry, strlen(entry)); used += strlen(entry); inserted = true;
        }
        size_t bytes = n + (end != NULL);
        if (type != 2) { memcpy(buffer + used, s + i, bytes); used += bytes; }
        i += bytes;
    }
    if (enable && !inserted) {
        if (used && buffer[used - 1] != '\n') buffer[used++] = '\n';
        if (!section) { memcpy(buffer + used, "[AutoRun]\n", 10); used += 10; }
        memcpy(buffer + used, entry, strlen(entry)); used += strlen(entry);
    }
    buffer[used] = 0; *out = buffer; *length = used;
    return 0;
}
