// Environment variables: getenv/setenv, and exec (which searches PATH).
// Kept out of ulib.c because setenv needs malloc, and some programs
// (forktest) link ulib.o without umalloc.o.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

extern char **environ; // defined in ulib.c, set up by start()

char* getenv(const char *name)
{
  int i;
  int len = strlen(name);
  for (i = 0; environ[i]; i++) {
    if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
      return &environ[i][len + 1];
    }
  }
  return 0;
}

void setenv(const char *name, const char *value)
{
  int i;
  int len = strlen(name);
  for (i = 0; environ[i]; i++) {
    if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
      // Found existing variable, update its value
      char *new_var = (char*)malloc(len + strlen(value) + 2); // +2 for '=' and '\0'
      strcpy(new_var, name);
      new_var[len] = '=';
      strcpy(new_var + len + 1, value);
      environ[i] = new_var;
      return;
    }
  }
  // Variable not found, add a new one
  char **new_environ = (char**)malloc((i + 2) * sizeof(char*)); // +2 for new var and NULL terminator
  for (int j = 0; j < i; j++) {
    new_environ[j] = environ[j];
  }
  char *new_var = (char*)malloc(len + strlen(value) + 2); // +2 for '=' and '\0'
  strcpy(new_var, name);
  new_var[len] = '=';
  strcpy(new_var + len + 1, value);
  new_environ[i] = new_var;
  new_environ[i + 1] = 0;
  environ = new_environ;
}

void unsetenv(const char *name)
{
  int i, n = 0;
  int len = strlen(name);
  for (i = 0; environ[i]; i++) {
    if (strncmp(environ[i], name, len) != 0 || environ[i][len] != '=')
      environ[n++] = environ[i];
  }
  environ[n] = 0;
}

#define DEFAULT_PATH "/bin"

// Like execve, but with the current environment, and if file contains
// no '/', search the colon-separated directories in $PATH for it
// (an empty entry means the current directory). Returns -1 if every
// candidate fails; on success it does not return.
int exec(char *file, char **argv)
{
  char buf[MAXPATH];
  char *path, *dir, *end;
  int dirlen, filelen;

  if (strchr(file, '/'))
    return execve(file, argv, environ);

  path = getenv("PATH");
  if (path == 0)
    path = DEFAULT_PATH;

  filelen = strlen(file);
  for (dir = path;; dir = end + 1) {
    end = strchr(dir, ':');
    if (end == 0)
      end = dir + strlen(dir);
    dirlen = end - dir;

    if (dirlen == 0) {
      // Empty entry: current directory.
      execve(file, argv, environ);
    } else if (dirlen + 1 + filelen < MAXPATH) {
      memmove(buf, dir, dirlen);
      buf[dirlen] = '/';
      memmove(buf + dirlen + 1, file, filelen + 1);
      execve(buf, argv, environ);
    }
    // execve only returns on failure; try the next entry.

    if (*end == '\0')
      break;
  }
  return -1;
}
