/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Starts a program from this folder in the background, its output going to
 * <program>.log beside it, and returns at once. The runner task runs one
 * program at a time; this is how a daemon and its client run together.
 *   detach <program> [args...] */
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(int argc, char **argv) {
  if (argc < 2 || strspn(argv[1], "abcdefghijklmnopqrstuvwxyz0123456789_") !=
                      strlen(argv[1])) {
    fprintf(stderr, "usage: detach <program> [args...]\n");
    return 2;
  }
  char dir[MAX_PATH], exe[MAX_PATH], log[MAX_PATH], command[8192];
  DWORD n = GetModuleFileNameA(NULL, dir, sizeof(dir));
  char *slash = n && n < sizeof(dir) ? strrchr(dir, '\\') : NULL;
  if (!slash) return 2;
  *slash = 0;
  snprintf(exe, sizeof(exe), "%s\\%s.exe", dir, argv[1]);
  snprintf(log, sizeof(log), "%s\\%s.log", dir, argv[1]);

  size_t used = (size_t)snprintf(command, sizeof(command), "\"%s\"", exe);
  for (int i = 2; i < argc; i++) {
    if (strchr(argv[i], '"') || used + strlen(argv[i]) + 4 > sizeof(command))
      return 2;
    used += (size_t)snprintf(command + used, sizeof(command) - used, " \"%s\"",
                             argv[i]);
  }

  /* The child must not hold this program's own output open, or whoever waits
   * for that output waits for the child. */
  const DWORD standard[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  for (int i = 0; i < 3; i++)
    SetHandleInformation(GetStdHandle(standard[i]), HANDLE_FLAG_INHERIT, 0);

  SECURITY_ATTRIBUTES inherit = {sizeof(inherit), NULL, TRUE};
  HANDLE out = CreateFileA(log, GENERIC_WRITE, FILE_SHARE_READ, &inherit,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (out == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "detach: cannot write %s (error %lu)\n", log, GetLastError());
    return 1;
  }
  STARTUPINFOA start = {sizeof(start)};
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = start.hStdError = out;
  PROCESS_INFORMATION process;
  if (!CreateProcessA(exe, command, NULL, NULL, TRUE,
                      DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, dir,
                      &start, &process)) {
    fprintf(stderr, "detach: cannot start %s (error %lu)\n", exe, GetLastError());
    return 1;
  }
  printf("started %s as process %lu\n", argv[1], process.dwProcessId);
  return 0;
}
