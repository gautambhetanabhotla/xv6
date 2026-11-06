// Tests for environment variables (getenv/setenv) across fork.

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

int
main(int argc, char *argv[])
{
  test_inherit();
  test_isolate();

  if (failures) {
    printf("envtest: %d test(s) FAILED\n", failures);
    exit(1);
  }
  printf("envtest: ALL TESTS PASSED\n");
  exit(0);
}
