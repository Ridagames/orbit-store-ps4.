/* Orbit's certificate list for HTTPS, read once and shared (see https.h). */
#include "https.h"
#include "ca.h"
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>

static X509_STORE *shared; /* set once on the main thread, then only read */

/* The built-in list; tests may name another with ORBIT_TEST_CA_FILE. */
static const char *bundle(size_t *size) {
#ifdef ORBIT_TEST
    static char *test;
    static size_t test_size;
    const char *path = getenv("ORBIT_TEST_CA_FILE");
    if (path && *path && !test) {
        FILE *f = fopen(path, "rb");
        if (f) {
            test = calloc(1, 1 << 20);
            test_size = test ? fread(test, 1, (1 << 20) - 1, f) : 0;
            fclose(f);
        }
    }
    if (test) {
        *size = test_size;
        return test;
    }
#endif
    *size = sizeof orbit_ca - 1;
    return orbit_ca;
}

int https_init(void) {
    if (shared)
        return 0;
    size_t size = 0;
    const char *text = bundle(&size);
    BIO *bio = BIO_new_mem_buf(text, (int)size);
    STACK_OF(X509_INFO) *certificates = bio ? PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL) : NULL;
    BIO_free(bio);
    X509_STORE *store = certificates ? X509_STORE_new() : NULL;
    int added = 0;
    for (int i = 0; store && i < sk_X509_INFO_num(certificates); i++) {
        X509_INFO *info = sk_X509_INFO_value(certificates, i);
        if (info->x509 && X509_STORE_add_cert(store, info->x509))
            added++;
    }
    sk_X509_INFO_pop_free(certificates, X509_INFO_free);
    if (!added) {
        X509_STORE_free(store);
        return -1;
    }
    /* As curl sets up its own lists by default (without CURLSSLOPT_NO_PARTIALCHAIN). */
    X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN);
    shared = store;
    return 0;
}

/* curl's last step in setting up a connection's TLS: swap in the shared list. */
static CURLcode use_shared(CURL *curl, void *ssl_ctx, void *unused) {
    (void)curl;
    (void)unused;
    SSL_CTX_set1_cert_store((SSL_CTX *)ssl_ctx, shared); /* takes its own reference */
    return CURLE_OK;
}

void https_use_orbit_ca(CURL *curl) {
    if (shared) {
        /* No file or folder of certificates: only the shared list is trusted. */
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, use_shared);
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, NULL);
        return;
    }
    size_t size = 0;
    const char *text = bundle(&size);
    struct curl_blob ca = {(void *)text, size, CURL_BLOB_COPY};
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
}
