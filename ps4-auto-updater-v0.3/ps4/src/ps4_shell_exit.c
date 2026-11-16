/*
 * Explicit PS4 app lifecycle exit.
 *
 * Primary evidence:
 *  - ItemzFlow itemz-loader/source/main.c uses
 *    sceSystemServiceLoadExec("exit", 0).
 *  - OpenOrbis-compatible PS4 hardware reports SIGSYS in libc _exit
 *    when main() returns (orbis-ports/orbis-compat).
 *
 * Never invoke raw syscalls or return to the libc exit trampoline.
 * This module only requests app termination, never system power actions.
 */
#include "ps4_shell_exit.h"
#include <stdint.h>
#include <stddef.h>

#if defined(PS4_EXIT_HOST_TEST)
#include "ps4_shell_exit_mock.h"
#elif defined(__ORBIS__)
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>
#else
#error "ps4_shell_exit is PS4-only except when PS4_EXIT_HOST_TEST is set"
#endif

void ps4_shell_exit(int status, Ps4ShellExitTrace trace) {
    if (trace) trace("PS4_SHELL_EXIT_STATUS", status);

    /* SystemService is the shell-owned app lifecycle interface.  A
     * linked import can be available even when an explicit load returns
     * an error, so record both steps and let the shell report its rc. */
    uint32_t module_rc = sceSysmoduleLoadModuleInternal(
        ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE
    );
    if (trace) trace("PS4_SHELL_EXIT_MODULE_RC", (int)module_rc);

    if (trace) trace("PS4_SHELL_EXIT_REQUEST_BEGIN", 1);
    int32_t result = sceSystemServiceLoadExec("exit", NULL);
    if (trace) trace("PS4_SHELL_EXIT_REQUEST_RC", result);

    /* A successful request can complete asynchronously. A failed one
     * must not pass control back into musl's _exit, which has produced
     * CE-34878-0 on PS4 hardware. Leave user to PS -> Close Application. */
    if (trace) trace("PS4_SHELL_EXIT_IDLE_FALLBACK", result);
    for (;;) {
        (void)sceKernelUsleep(1000000);
    }
}
