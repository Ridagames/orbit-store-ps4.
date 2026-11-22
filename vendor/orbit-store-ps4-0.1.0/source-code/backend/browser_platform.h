#ifndef ORBIT_BROWSER_NATIVE_H
#define ORBIT_BROWSER_NATIVE_H
/* System browser and application launching on PS4 (browser_services.c).
 * Orbit never reads another process's memory: a provider's download link
 * comes back from Orbit Store Browser through Sony's browser window. */
#include "browser_scan.h"
int probe_browser_open(const char *url);
int probe_browser_user(void);
/* Starts an ORBT title for the foreground user; 0 on success. */
int probe_app_launch(const char *title_id);
/* Whether an ORBT title is running: 1 yes, 0 no, negative when the console
 * can't say. */
int probe_app_running(const char *title_id);
/* Closes a running ORBT title and waits up to ten seconds for it to go; 0 once
 * it is not running. Used only to replace the TV app with a newer release. */
int probe_app_close(const char *title_id);
#endif
