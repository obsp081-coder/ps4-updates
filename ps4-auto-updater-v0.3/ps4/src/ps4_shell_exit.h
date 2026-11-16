#ifndef PS4_SHELL_EXIT_H
#define PS4_SHELL_EXIT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Ps4ShellExitTrace)(const char *name, int value);

/* A PS4 homebrew must not fall through libc _exit after main().
 * Ask the shell to close the title; on failure, remain idle so the user
 * can select Close Application from the PS button menu. */
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noreturn))
#endif
void ps4_shell_exit(int status, Ps4ShellExitTrace trace);

#ifdef __cplusplus
}
#endif
#endif
