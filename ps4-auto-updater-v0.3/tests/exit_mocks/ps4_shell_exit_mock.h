#ifndef PS4_SHELL_EXIT_MOCK_H
#define PS4_SHELL_EXIT_MOCK_H

#include <stdint.h>
#define ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE 0x80000010u

uint32_t sceSysmoduleLoadModuleInternal(uint32_t module);
int32_t sceSystemServiceLoadExec(const char *path, const char *args[]);
int32_t sceKernelUsleep(uint32_t delay);

#endif
