#include <sysdeps.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <str/str.h>

#include "bcron.h"

const char program[] = "bcron-start";
const int msg_show_pid = 0;

int main(int argc, char* argv[])
{
  str path = {0,0,0};
  int pipe1[2];			/* From -sched to -exec */
#ifdef IGNORE_EXEC
  int devnull;
#else
  int pipe2[2];			/* From -exec to -sched */
#endif
  struct passwd* pw;
  int slash;
  const char* user;

  if ((devnull = open("/dev/null", O_RDWR, 0)) == -1)
    die1sys(111, "Could not open '/dev/null' for writing");

  wrap_str(str_copys(&path, argv[0]));
  if ((slash = str_findlast(&path, '/')) < 0)
    path.len = slash = 0;
  else
    path.len = ++slash;
  
  if ((user = getenv("BCRON_USER")) == 0)
    user = BCRON_USER;
  if ((pw = getpwnam(user)) == 0)
    die1(111, "Could not look up cron user");

  if (pipe(pipe1) != 0)
    die1sys(111, "Could not create pipe");

  wrap_str(str_cats(&path, "bcron-exec"));
  switch (fork()) {
  case -1:
    die1sys(111, "Could not fork");
  case 0:
    dup2(pipe1[0], 0);
    close(pipe1[0]);
    close(pipe1[1]);
#ifdef IGNORE_EXEC
    dup2(devnull, 1);
    close(devnull);
#else
    dup2(pipe2[1], 1);
    close(pipe2[0]);
    close(pipe2[1]);
#endif
    argv[0] = path.s;
    execvp(argv[0], argv);
    die1sys(111, "Could not exec bcron-exec");
  }

  if (setgid(pw->pw_gid) != 0
      || setuid(pw->pw_uid) != 0)
    die1sys(111, "Could not setuid");
  
  path.len = slash;
  wrap_str(str_cats(&path, "bcron-sched"));
  argv[0] = path.s;
  argv[1] = 0;
  dup2(pipe1[1], 1);
  close(pipe1[0]);
  close(pipe1[1]);
#ifdef IGNORE_EXEC
  dup2(devnull, 0);
  close(devnull);
#else
  dup2(pipe2[0], 0);
  close(pipe2[0]);
  close(pipe2[1]);
#endif
  execvp(argv[0], argv);
  die1sys(111, "Could not exec bcron-sched");
  return 111;
  (void)argc;
}
