/* Native PS4 SystemService exit handshake, tested without real hardware.
 * A mock longjmp breaks the deliberately infinite standby fallback. */
#include "ps4_shell_exit_mock.h"
#include "../../ps4/src/ps4_shell_exit.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static jmp_buf exit_frame;
static int expected_status;
static int sysmodule_failure, service_failure;
static int exit_calls, sleep_calls, module_calls, traces;
static int trace_status, trace_modrc, trace_retrc;
static const char *trace_order[8];

static void write_trace(const char *label, int value) {
    if (traces < 8) trace_order[traces] = label;
    ++traces;
    if (strcmp(label, "PS4_SHELL_EXIT_STATUS") == 0)
        trace_status = value;
    if (strcmp(label, "PS4_SHELL_EXIT_MODULE_RC") == 0)
        trace_modrc = value;
    if (strcmp(label, "PS4_SHELL_EXIT_REQUEST_RC") == 0)
        trace_retrc = value;
}

uint32_t sceSysmoduleLoadModuleInternal(uint32_t module) {
    assert(module == ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    ++module_calls;
    return sysmodule_failure ? 0x80020008u : 0;
}
int32_t sceSystemServiceLoadExec(const char *path, const char *args[]) {
    ++exit_calls;
    assert(strcmp(path, "exit") == 0 && args == NULL);
    return service_failure ? (int32_t)0x80290009u : 0;
}
int32_t sceKernelUsleep(uint32_t us) {
    assert(us == 1000000);
    ++sleep_calls;
    longjmp(exit_frame, 1);
}

static void scenario(int status, int module_error, int service_error) {
    expected_status = status;
    sysmodule_failure = module_error;
    service_failure = service_error;
    exit_calls = module_calls = sleep_calls = traces = 0;
    trace_status = trace_modrc = trace_retrc = 0;
    if (setjmp(exit_frame) == 0) {
        ps4_shell_exit(status, write_trace);
        assert(!"PS4 exit MUST NOT fall through libc to main()");
    }
    assert(trace_status == expected_status);
    assert(module_calls == 1 && exit_calls == 1 && sleep_calls == 1);
    assert(traces == 5);
    assert(strcmp(trace_order[0], "PS4_SHELL_EXIT_STATUS") == 0);
    assert(strcmp(trace_order[1], "PS4_SHELL_EXIT_MODULE_RC") == 0);
    assert(strcmp(trace_order[2], "PS4_SHELL_EXIT_REQUEST_BEGIN") == 0);
    assert(strcmp(trace_order[3], "PS4_SHELL_EXIT_REQUEST_RC") == 0);
    assert(strcmp(trace_order[4], "PS4_SHELL_EXIT_IDLE_FALLBACK") == 0);
    assert((uint32_t)trace_modrc == (module_error ? 0x80020008u : 0));
    assert((uint32_t)trace_retrc == (service_error ? 0x80290009u : 0));
}

int main(void) {
    scenario(0, 0, 0);
    scenario(1, 0, 1);
    scenario(2, 1, 0);
    scenario(1, 1, 1);
    puts("PS4_SHELL_EXIT_HOST_OK");
    return 0;
}
