#include "install.h"
#include <stdio.h>

static char last_error[192];

const char *orbit_launcher_last_error(void) {
    return last_error;
}

void orbit_launcher_record_error(const char *step, int system_error, int platform_error) {
    snprintf(last_error, sizeof last_error, "%s: errno=%d, platform=0x%08x", step, system_error,
             (unsigned)platform_error);
}
