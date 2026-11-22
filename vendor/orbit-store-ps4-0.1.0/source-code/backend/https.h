/* Orbit's certificate list for HTTPS, read once and shared.
 *
 * Every connection used to read the whole built-in list (about 150 certificates)
 * on its first use. When Orbit starts, a dozen threads did that at the same
 * moment, and on a PS4 one of them crashed inside OpenSSL while reading it
 * (firmware 12.00, 13 November 2026: TorBox's account check, x509_name_canon).
 * The list is now read once on the main thread, before any other thread starts,
 * and every connection uses that one copy. It also saves each thread its own copy
 * in memory. */
#pragma once
#include <curl/curl.h>

/* Main thread, after curl_global_init and before any thread starts. 0 when the
 * shared list is ready; otherwise connections read the list themselves, as before. */
int https_init(void);

/* Every HTTPS handle, after curl_easy_init or curl_easy_reset: trust exactly
 * Orbit's certificate list. Peer and host checks stay with the caller. */
void https_use_orbit_ca(CURL *curl);
