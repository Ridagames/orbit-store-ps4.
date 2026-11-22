#ifndef ORBIT_DNS_MESSAGE_H
#define ORBIT_DNS_MESSAGE_H
/* DNS messages for Orbit's own IPv4 host lookups on the PS4 (backend/ps4_dns.c):
 * one A question out, the host's A answers back. Pure functions, no I/O. */
#include <stddef.h>
#include <stdint.h>

/* An A/IN question for host, recursion asked. Returns its length, or -1 when
 * the name can't be sent (empty label, label over 63 bytes, name over 253) or
 * cap is too small. */
int dns_query(uint16_t id, const char *host, uint8_t *out, size_t cap);

enum { DNS_FOUND = 0, DNS_NO_ADDRESS = 1, DNS_UNUSABLE = -1 };
/* The most addresses one answer gives back. */
enum { DNS_MOST_ADDRESSES = 8 };
/* Reads a reply to the dns_query with this id and host. DNS_FOUND sets
 * addresses[0..*count) (network byte order, in the reply's order, no repeats,
 * at most cap) from the host's A/IN answers, following any CNAMEs the server
 * put before them, and *ttl (seconds) to the shortest of theirs. A site with
 * several addresses gives them all, so a caller can move on when one doesn't
 * answer. DNS_NO_ADDRESS: the name doesn't exist, or has no IPv4 address.
 * DNS_UNUSABLE: malformed, for another question, a server failure, or cut
 * short with no address in it; the caller waits for another reply or asks
 * another server. */
int dns_answer(const uint8_t *reply, size_t length, uint16_t id, const char *host,
               uint32_t *addresses, int cap, int *count, uint32_t *ttl);
#endif
