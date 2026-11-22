#ifndef ORBIT_PS4_MODULES_H
#define ORBIT_PS4_MODULES_H
#include <stdint.h>
#include <stddef.h>
int ps4_module_private_pages(uintptr_t begin, uintptr_t end, size_t page_size);
/* On-demand, process-lifetime system modules. No native imports at startup. */
uint32_t ps4_module_load(const char *name);
void *ps4_module_symbol(uint32_t handle, const char *name);
int ps4_modules_initialize(void);
#endif
