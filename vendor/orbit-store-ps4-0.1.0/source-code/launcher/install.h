#ifndef ORBIT_INSTALL_H
#define ORBIT_INSTALL_H
/* Written only by startup; copy the result before publishing it to the API. */
const char *orbit_launcher_last_error(void);
const char *orbit_launcher_registration_method(void);
void orbit_launcher_record_error(const char *step, int system_error, int platform_error);
int orbit_launcher_ensure(int state_fd);
#endif
