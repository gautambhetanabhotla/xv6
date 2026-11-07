// Tests for environment variables (getenv/setenv/unsetenv) across fork
// and exec. The exec tests run the helper program envcheck.

#include "kernel/types.h"
#include "user/user.h"

static int failures = 0;

static void
fail(const char *test, const char *msg)
{
  printf("%s: FAILED: %s\n", test, msg);
  failures++;
}

// Fork, run fn in the child, and return the child's exit status.
static int
runchild(int (*fn)(void))
{
  int pid, status;

  pid = fork();
  if (pid < 0) {
    printf("envtest: fork failed\n");
    exit(1);
  }
  if (pid == 0)
    exit(fn());
  if (wait(&status) != pid) {
    printf("envtest: wait failed\n");
    exit(1);
  }
  return status;
}

// Test 1: a variable set in the parent before fork is visible in the child.

static int
inherit_child(void)
{
  char *v = getenv("ENVTEST_INHERIT");
  if (v == 0) {
    printf("  child: ENVTEST_INHERIT not set\n");
    return 1;
  }
  printf("  child: ENVTEST_INHERIT=%s\n", v);
  if (strcmp(v, "from-parent") != 0)
    return 2;
  return 0;
}

static void
test_inherit(void)
{
  char *t = "inherit";

  printf("test %s\n", t);
  setenv("ENVTEST_INHERIT", "from-parent");
  int st = runchild(inherit_child);
  if (st == 1)
    fail(t, "child did not see variable set by parent");
  else if (st == 2)
    fail(t, "child saw wrong value");
  else if (st != 0)
    fail(t, "child exited abnormally");
  else
    printf("test %s: OK\n", t);
}

// Test 2: changes the child makes (overwriting an existing variable
// and adding a new one) do not show up in the parent.

static int
isolate_child(void)
{
  setenv("ENVTEST_SHARED", "child-value");
  setenv("ENVTEST_CHILDONLY", "1");

  // Sanity check: the child sees its own changes.
  char *v = getenv("ENVTEST_SHARED");
  if (v == 0 || strcmp(v, "child-value") != 0)
    return 1;
  if (getenv("ENVTEST_CHILDONLY") == 0)
    return 1;
  printf("  child: ENVTEST_SHARED=%s\n", v);
  return 0;
}

static void
test_isolate(void)
{
  char *t = "isolate";
  char *v;

  printf("test %s\n", t);
  setenv("ENVTEST_SHARED", "parent-value");
  if (runchild(isolate_child) != 0) {
    fail(t, "child's setenv did not take effect in the child");
    return;
  }

  v = getenv("ENVTEST_SHARED");
  if (v == 0) {
    fail(t, "parent lost ENVTEST_SHARED");
    return;
  }
  printf("  parent: ENVTEST_SHARED=%s\n", v);
  if (strcmp(v, "parent-value") != 0) {
    fail(t, "child's overwrite leaked into parent");
    return;
  }
  if (getenv("ENVTEST_CHILDONLY") != 0) {
    fail(t, "variable added by child leaked into parent");
    return;
  }
  printf("test %s: OK\n", t);
}

// Exit status of a child whose exec returned, as opposed to envcheck's
// 0 (environment as expected) or 1 (it wasn't).
#define EXEC_FAILED 100

// Report the result of a child that exec'd envcheck.
static int
checkexec(const char *test, int st)
{
  if (st == 0)
    return 1;
  if (st == EXEC_FAILED)
    fail(test, "exec failed");
  else if (st == 1)
    fail(test, "envcheck saw the wrong environment");
  else
    fail(test, "child exited abnormally");
  return 0;
}

// Test 3: exec passes the current environment on to the new program.

static int
exec_child(void)
{
  char *args[] = {"envcheck", "ENVTEST_EXEC=across-exec", "ENVTEST_EMPTY=",
                  "ENVTEST_INHERIT=from-parent", "ENVTEST_NOSUCHVAR", 0};
  exec("/bin/envcheck", args);
  return EXEC_FAILED;
}

static void
test_exec(void)
{
  char *t = "exec";

  printf("test %s\n", t);
  setenv("ENVTEST_EXEC", "across-exec");
  setenv("ENVTEST_EMPTY", "");
  if (checkexec(t, runchild(exec_child)))
    printf("test %s: OK\n", t);
}

// Test 4: changes made after fork but before exec reach the new
// program, and don't show up in the parent.

static int
execchange_child(void)
{
  char *args[] = {"envcheck", "ENVTEST_OVER=new", "ENVTEST_ADDED=1",
                  "ENVTEST_GONE", 0};
  setenv("ENVTEST_OVER", "new");
  setenv("ENVTEST_ADDED", "1");
  unsetenv("ENVTEST_GONE");
  exec("/bin/envcheck", args);
  return EXEC_FAILED;
}

static void
test_execchange(void)
{
  char *t = "execchange";
  char *v;

  printf("test %s\n", t);
  setenv("ENVTEST_OVER", "old");
  setenv("ENVTEST_GONE", "still-here");
  if (!checkexec(t, runchild(execchange_child)))
    return;

  v = getenv("ENVTEST_OVER");
  if (v == 0 || strcmp(v, "old") != 0) {
    fail(t, "child's overwrite leaked into parent");
    return;
  }
  if (getenv("ENVTEST_ADDED") != 0) {
    fail(t, "variable added by child leaked into parent");
    return;
  }
  if (getenv("ENVTEST_GONE") == 0) {
    fail(t, "child's unsetenv leaked into parent");
    return;
  }
  printf("test %s: OK\n", t);
}

// Test 5: execve gives the new program exactly the environment passed
// to it, replacing the caller's rather than adding to it.

static int
execve_child(void)
{
  char *args[] = {"envcheck", "ENVTEST_ONLY=1", "ENVTEST_EXEC", "PATH", 0};
  char *env[] = {"ENVTEST_ONLY=1", 0};
  execve("/bin/envcheck", args, env);
  return EXEC_FAILED;
}

static void
test_execve(void)
{
  char *t = "execve";

  printf("test %s\n", t);
  if (checkexec(t, runchild(execve_child)))
    printf("test %s: OK\n", t);
}

// Test 6: a program can start with an empty environment.

static int
execveempty_child(void)
{
  char *args[] = {"envcheck", "ENVTEST_EXEC", "PATH", 0};
  char *env[] = {0};
  execve("/bin/envcheck", args, env);
  return EXEC_FAILED;
}

static void
test_execveempty(void)
{
  char *t = "execveempty";

  printf("test %s\n", t);
  if (checkexec(t, runchild(execveempty_child)))
    printf("test %s: OK\n", t);
}

int
main(int argc, char *argv[])
{
  test_inherit();
  test_isolate();
  test_exec();
  test_execchange();
  test_execve();
  test_execveempty();

  if (failures) {
    printf("envtest: %d test(s) FAILED\n", failures);
    exit(1);
  }
  printf("envtest: ALL TESTS PASSED\n");
  exit(0);
}
