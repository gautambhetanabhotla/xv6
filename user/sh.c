// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));
char *expand(char *);
void expandargs(char **);
void exportvar(char *);
void unsetvar(char *);
void printenv(void);
char *splitlist(char *);
void runsimple(char *);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    expandargs(ecmd->argv);
    if (ecmd->argv[0] == 0)
      exit(1);
    if (strcmp(ecmd->argv[0], "export") == 0) {
      // Not run by the shell itself (e.g. part of a pipeline or in
      // parentheses), so it only affects this subshell.
      if (ecmd->argv[1] == 0)
        printenv();
      for (int i = 1; ecmd->argv[i]; i++)
        exportvar(ecmd->argv[i]);
      exit(0);
    }
    if (strcmp(ecmd->argv[0], "unset") == 0) {
      for (int i = 1; ecmd->argv[i]; i++)
        unsetvar(ecmd->argv[i]);
      exit(0);
    }
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(expand(rcmd->file), rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if (buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd, *next;
    // Run each ;-separated command from here rather than leaving them
    // to runcmd's LIST case, so that a cd or export takes effect in
    // the shell, in time for the commands after it.
    for (cmd = buf; cmd; cmd = next) {
      next = splitlist(cmd);
      runsimple(cmd);
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}

//PAGEBREAK!
// Environment variables and builtins

extern char **environ;

// Length of the variable name at the start of s, or 0 if there is none.
int
varlen(char *s)
{
  int n = 0;

  if (*s >= '0' && *s <= '9')
    return 0;
  while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') ||
         (s[n] >= '0' && s[n] <= '9') || s[n] == '_')
    n++;
  return n;
}

// Value of the variable whose name is the n characters at name.
// Unset variables have an empty value.
char *
lookup(char *name, int n)
{
  char c = name[n];
  char *v;

  name[n] = 0;
  v = getenv(name);
  name[n] = c;
  return v ? v : "";
}

// Return a newly allocated copy of word with each $NAME replaced by
// its value. A $ that does not start a name is left as it is.
char *
expand(char *word)
{
  char *p, *q, *v, *out;
  int n, len = 1;

  for (p = word; *p;) {
    if (*p == '$' && (n = varlen(p + 1)) > 0) {
      len += strlen(lookup(p + 1, n));
      p += n + 1;
    } else {
      len++;
      p++;
    }
  }

  out = malloc(len);
  for (p = word, q = out; *p;) {
    if (*p == '$' && (n = varlen(p + 1)) > 0) {
      v = lookup(p + 1, n);
      strcpy(q, v);
      q += strlen(v);
      p += n + 1;
    } else {
      *q++ = *p++;
    }
  }
  *q = 0;
  return out;
}

// Expand each word of argv. As in other shells, a word that expands to
// nothing is removed instead of becoming an empty argument.
void
expandargs(char **argv)
{
  int i, n = 0;

  for (i = 0; argv[i]; i++) {
    char *w = expand(argv[i]);
    if (*w)
      argv[n++] = w;
  }
  argv[n] = 0;
}

// Handle one argument of export: NAME=VALUE sets a variable. A plain
// NAME is accepted but does nothing, as there are no shell variables
// to promote to the environment.
void
exportvar(char *word)
{
  char *eq = strchr(word, '=');
  int n = varlen(word);

  if (n == 0 || (eq ? word + n != eq : word[n] != 0)) {
    fprintf(2, "export: bad variable name: %s\n", word);
    return;
  }
  if (eq) {
    *eq = 0;
    setenv(word, eq + 1);
    *eq = '=';
  }
}

// Handle one argument of unset. Unsetting a variable that isn't set
// is not an error.
void
unsetvar(char *word)
{
  if (varlen(word) == 0 || word[varlen(word)] != 0) {
    fprintf(2, "unset: bad variable name: %s\n", word);
    return;
  }
  unsetenv(word);
}

void
printenv(void)
{
  for (int i = 0; environ[i]; i++)
    printf("%s\n", environ[i]);
}

// NUL-terminate s at its first ';' outside parentheses, and return the
// text after it. Return 0 if there is no such ';'.
char *
splitlist(char *s)
{
  int depth = 0;

  for (; *s; s++) {
    if (*s == '(') {
      depth++;
    } else if (*s == ')') {
      depth--;
    } else if (*s == ';' && depth <= 0) {
      *s = 0;
      return s + 1;
    }
  }
  return 0;
}

// True if s is the builtin called name, in a form the shell can run
// itself, which it can't if it has to fork for a pipe, redirection and
// so on.
int
isbuiltin(char *s, char *name)
{
  int n = strlen(name);

  if (strncmp(s, name, n) != 0 ||
      (s[n] != 0 && strchr(whitespace, s[n]) == 0))
    return 0;
  for (; *s; s++)
    if (strchr(symbols, *s))
      return 0;
  return 1;
}

// Call fn on each expanded argument of the builtin command s, in the
// shell itself. This doesn't use parsecmd, since a panic there would
// exit the shell rather than a forked child.
void
runbuiltin(char *s, void (*fn)(char *))
{
  char *es = s + strlen(s);
  char *q, *eq, *w;

  gettoken(&s, es, 0, 0); // skip the builtin's name
  while (gettoken(&s, es, &q, &eq) == 'a') {
    *eq = 0;
    w = expand(q);
    if (*w)
      fn(w);
    free(w);
  }
}

// Run one command that has no top-level ';', and wait for it.
void
runsimple(char *cmd)
{
  char *e, *dir;

  while (*cmd && strchr(whitespace, *cmd))
    cmd++;
  e = cmd + strlen(cmd);
  while (e > cmd && strchr(whitespace, e[-1]))
    *--e = 0;
  if (*cmd == 0) // is a blank command
    return;

  if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
    // Chdir must be called by the parent, not the child.
    dir = expand(cmd + 3);
    if (chdir(dir) < 0)
      fprintf(2, "cannot cd %s\n", dir);
    free(dir);
  } else if (isbuiltin(cmd, "export")) {
    // Likewise changing the environment.
    if (strcmp(cmd, "export") == 0)
      printenv();
    runbuiltin(cmd, exportvar);
  } else if (isbuiltin(cmd, "unset")) {
    runbuiltin(cmd, unsetvar);
  } else {
    if (fork1() == 0)
      runcmd(parsecmd(cmd));
    wait(0);
  }
}
