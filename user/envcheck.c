// Check this process's environment against the arguments, for envtest
// to exec, so that the check runs in a program that received its
// environment through execve.
//
// usage: envcheck [NAME=VALUE | NAME]...
//   NAME=VALUE  NAME must be set to exactly VALUE
//   NAME        NAME must not be set
//
// Exits with 0 if every check passes, and 1 otherwise.

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i, bad = 0;

  for (i = 1; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    char *v;

    if (eq == 0) {
      v = getenv(argv[i]);
      if (v) {
        printf("  envcheck: %s should be unset, but is '%s'\n", argv[i], v);
        bad = 1;
      } else {
        printf("  envcheck: %s is unset\n", argv[i]);
      }
      continue;
    }

    *eq = 0;
    v = getenv(argv[i]);
    if (v == 0) {
      printf("  envcheck: %s should be '%s', but is unset\n", argv[i], eq + 1);
      bad = 1;
    } else if (strcmp(v, eq + 1) != 0) {
      printf("  envcheck: %s should be '%s', but is '%s'\n", argv[i], eq + 1, v);
      bad = 1;
    } else {
      printf("  envcheck: %s=%s\n", argv[i], v);
    }
  }
  exit(bad);
}
