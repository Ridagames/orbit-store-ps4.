#ifndef ORBIT_PACKAGES_H
#define ORBIT_PACKAGES_H
#include "orbit.h"
/* Receipts are created only after a download/receiver has published its files. */
void packages_init(void);
int packages_start(void);
void packages_stop(void);
void packages_completed(const Job *pin, const char *origin, const char *output, bool scan);
void packages_auto_install(const char *origin, const char *after);
cJSON *packages_for_download(const char *origin);
cJSON *packages_status(void);
cJSON *packages_remove_files(const cJSON *input, int *code, char *error, size_t cap);
int packages_stream(const cJSON *input, const char *peer, char *error, size_t cap);
int packages_action(const cJSON *input, char *error, size_t cap);
int packages_download_action(const char *origin, const char *action, char *error, size_t cap);
bool packages_path_busy(const char *path);
/* Files: install the package, game image or game folder at root/folder/output
 * (folder "." is the drive's top). */
int packages_install_path(const Job *pin, const char *folder, const char *output, const char *origin,
                          char *error, size_t cap);
/* Console installer boundary. Serializes TV-app and download worker calls. */
typedef struct {
    char state[24], message[256];
    uint64_t received, total;
    int error;
} PackageProgress;
bool package_platform_available(void);
int package_platform_installed(const char *title_id, bool *installed);
/* Uninstalls an application; 0 on success. Orbit's own helper only. */
int package_platform_uninstall(const char *title_id);
int package_platform_submit(const char *path, const char *name, const char *content_id,
                            char result_id[49], char *error, size_t cap);
/* A package Orbit Zero serves over HTTP, described by its header. */
typedef struct { const char *package_type; uint64_t size; bool patch; } PackageRemote;
int package_platform_submit_url(const char *url, const char *name, const char *content_id,
                                const PackageRemote *remote, char result_id[49], char *error, size_t cap);
int package_platform_poll(const char *result_id, PackageProgress *result);
void package_platform_close(void);
#endif
