#include <sysdeps.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <path/path.h>
#include <str/env.h>
#include <str/str.h>
#include <unix/nonblock.h>
#include <unix/selfpipe.h>
#include <unix/sig.h>

#include "bcron.h"

const char program[] = "bcron-exec";
const int msg_show_pid = 0;

extern char** environ;

static const char** shell_argv;
static int shell_argc;

#define SLOT_MAX 512
struct slot 
{
  pid_t pid;
  int tmpfd;
  long headerlen;
  int sending_email;
  str id;
  struct passwd pw;
};
static struct slot slots[SLOT_MAX];

static const char* sendmail[7] = {
  "/usr/sbin/sendmail", "-FCronDaemon", "-i", "-odi", "-oem", "-t", 0
};

/*
  Input: ID NUL username NUL command NUL environment NUL NUL

  Output: ID NUL code message NUL

  Codes:
  K Job completed
  Z Temporary error running job
  D Invalid job specification
*/

static const char* path;

static str tmp;

static const char* tmpprefix = "/tmp/bcron";

#define NUL 0
static int devnull;

static void report(const char* id, const char* msg)
{
  wrap_str(str_copyb(&tmp, id, strlen(id) + 1));
  wrap_str(str_cats(&tmp, msg));
  if (!sendpacket(1, &tmp))
    exit(111);
}

static void failsys(const char* id, const char* msg)
{
  wrap_str(str_copyb(&tmp, id, strlen(id) + 1));
  wrap_str(str_cats(&tmp, msg));
  wrap_str(str_cats(&tmp, ": "));
  wrap_str(str_cats(&tmp, strerror(errno)));
  if (!sendpacket(1, &tmp))
    exit(111);
}

static void init_slots(void)
{
  int slot;
  for (slot = 0; slot < SLOT_MAX; ++slot) {
    slots[slot].pid = 0;
    slots[slot].tmpfd = -1;
    wrap_str(str_ready(&slots[slot].id, 16));
  }
}

static int pick_slot(void)
{
  int slot;
  for (slot = 0; slot < SLOT_MAX; ++slot)
    if (slots[slot].pid == 0)
      return slot;
  return -1;
}

static void exec_cmd(int fdin, int fdout,
		     const char** argv,
		     const str* env,
		     const struct passwd* pw)
{
  gid_t groups[256];
  int ngroups;
  ngroups = sizeof groups / sizeof *groups;
  if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) == -1)
    die1(111, "Could not determine group list");
  if (setgid(pw->pw_gid) == -1) die1sys(111, "Could not setgid");
  if (setgroups(ngroups, groups) != 0)
    die1sys(111, "Could not set supplementary groups");
  if (setuid(pw->pw_uid) == -1) die1sys(111, "Could not setuid");
  if (chdir(pw->pw_dir) == -1) die1sys(111, "Could not change directory");
  if (env)
    if ((environ = envstr_make_array(env)) == 0)
      die_oom(111);
  dup2(fdin, 0);
  close(fdin);
  dup2(fdout, 1);
  dup2(fdout, 2);
  close(fdout);
  execv(argv[0], (char**)argv);
  die3sys(111, "Could not execute '", argv[0], "'");
  exit(111);
}

static int forkexec_slot(int slot, int fdin, int fdout,
			 const char** argv,
			 const str* env)
{
  pid_t pid;
  const struct passwd* pw = &slots[slot].pw;
  switch (pid = fork()) {
  case -1:
    failsys(slots[slot].id.s, "ZFork failed");
    return 0;
  case 0:
    exec_cmd(fdin, fdout, argv, env, pw);
  }
  slots[slot].pid = pid;
  return 1;
}

static int strcopy(char** dest, const char* src)
{
  if (*dest)
    free(*dest);
  return (*dest = strdup(src)) != 0;
}

static int init_slot(int slot, const struct passwd* pw)
{
  struct slot* s;
  s = &slots[slot];
  if (!strcopy(&s->pw.pw_name, pw->pw_name))
    return 0;
  s->pw.pw_passwd = 0;
  s->pw.pw_uid = pw->pw_uid;
  s->pw.pw_gid = pw->pw_gid;
  s->pw.pw_gecos = 0;
  if (!strcopy(&s->pw.pw_dir, pw->pw_dir))
    return 0;
  s->pw.pw_shell = 0;
  return 1;
}

static void start_slot(int slot,
		       const char* command,
		       const char* envstart)
{
  static str env;
  char* period;
  int fd;
  char hostname[256];
  const char* mailto;
  const struct passwd* pw = &slots[slot].pw;
  const char* shell;
  
  msg5("(", pw->pw_name, ") CMD (", command, ")");

  env.len = 0;
  wrap_str(envstr_set(&env, "PATH", path, 1));
  if (envstart)
    wrap_str(envstr_from_string(&env, envstart, 1));
  wrap_str(envstr_set(&env, "HOME", pw->pw_dir, 1));
  wrap_str(envstr_set(&env, "USER", pw->pw_name, 1));
  wrap_str(envstr_set(&env, "LOGNAME", pw->pw_name, 1));

  if ((shell = envstr_get(&env, "SHELL")) == 0)
    shell = "/bin/sh";
  if ((mailto = envstr_get(&env, "MAILTO")) == 0)
    mailto = pw->pw_name;
  
  if (*mailto == 0) {
    fd = devnull;
    slots[slot].headerlen = 0;
  }
  else {
    if ((fd = path_mktemp(tmpprefix, &tmp)) == -1) {
      failsys(slots[slot].id.s, "ZCould not create temporary file");
      return;
    }
    unlink(tmp.s);
    gethostname(hostname, sizeof hostname);
    wrap_str(str_copyns(&tmp, 6, "To: <", mailto, ">\n",
			"From: Cron Daemon <root@", hostname, ">\n"));
    if ((period = strchr(hostname, '.')) != 0)
      *period = 0;
    wrap_str(str_catns(&tmp, 7, "Subject: Cron <", pw->pw_name,
		       "@", hostname, "> ", command, "\n\n"));
    slots[slot].headerlen = tmp.len;
    if (write(fd, tmp.s, tmp.len) != (long)tmp.len) {
      close(fd);
      fd = -1;
      report(slots[slot].id.s, "ZCould not write message header");
      return;
    }
  }
  
  shell_argv[shell_argc+0] = shell;
  shell_argv[shell_argc+1] = "-c";
  shell_argv[shell_argc+2] = command;

  if (!forkexec_slot(slot, devnull, fd, shell_argv, &env)) {
    if (fd != devnull)
      close(fd);
    fd = -1;
  }
  slots[slot].sending_email = 0;
  slots[slot].tmpfd = fd;
}

static void end_slot(int slot, int status)
{
  struct stat st;
  slots[slot].pid = 0;
  if (slots[slot].sending_email) {
    slots[slot].sending_email = 0;
    if (status)
      report(slots[slot].id.s, "ZJob complete, sending email failed");
    else
      report(slots[slot].id.s, "KJob complete, email sent");
  }
  else {
    if (slots[slot].headerlen == 0)
      report(slots[slot].id.s, "KJob complete");
    else if (fstat(slots[slot].tmpfd, &st) == -1)
      failsys(slots[slot].id.s, "ZCould not fstat");
    else if (st.st_size > slots[slot].headerlen) {
      if (lseek(slots[slot].tmpfd, SEEK_SET, 0) != 0)
	failsys(slots[slot].id.s, "ZCould not lseek");
      else {
	forkexec_slot(slot, slots[slot].tmpfd, devnull, sendmail, 0);
	slots[slot].sending_email = 1;
      }
    }
    else
      report(slots[slot].id.s, "KJob complete");
    /* To simplify the procedure, close the temporary file early.
     * The email sender still has it open, and will effect the final
     * deletion of the file when it completes. */
    close(slots[slot].tmpfd);
    slots[slot].tmpfd = -1;
  }
}

#define FAIL(S,M) do{ report(S,M); return; }while(0)

static void handle_packet(struct connection* c)
{
  str* packet;
  const char* id;
  const char* runas;
  const char* command;
  const char* envstart;
  long slot;
  unsigned int i;
  struct passwd* pw;

  packet = &c->packet;
  id = packet->s;
  if (*id == NUL)
    FAIL(id, "DInvalid ID");

  if ((slot = pick_slot()) < 0)
    FAIL(id, "DCould not allocate a slot");
  wrap_str(str_copys(&slots[slot].id, id));

  if ((i = str_findfirst(packet, NUL) + 1) >= packet->len)
    FAIL(id, "DInvalid packet");
  runas = packet->s + i;
  if (*runas == NUL
      || (pw = getpwnam(runas)) == 0)
    FAIL(id, "DInvalid username");

  if ((i = str_findnext(packet, NUL, i) + 1) >= packet->len)
    FAIL(id, "DInvalid packet");
  command = packet->s + i;

  if ((i = str_findnext(packet, NUL, i) + 1) >= packet->len)
    envstart = 0;
  else {
    envstart = packet->s + i;
    wrap_str(str_catc(packet, 0));
  }

  if (!init_slot(slot, pw))
    FAIL(id, "ZOut of memory");
  start_slot(slot, command, envstart);
}

static void handle_child(void)
{
  pid_t pid;
  int slot;
  int status;
  while ((pid = waitpid(-1, &status, WNOHANG)) != -1 && pid != 0) {
    for (slot = 0; slot < SLOT_MAX; ++slot) {
      if (slots[slot].pid == pid) {
	end_slot(slot, status);
	break;
      }
    }
  }
}

int main(int argc, char* argv[])
{
  struct connection conn;
  iopoll_fd fds[2];
  int selfpipe;
  int i;

  if ((shell_argv = malloc((argc + 3) * sizeof *argv)) == 0)
    die_oom(111);
  for (i = 1; i < argc; ++i)
    shell_argv[i-1] = argv[i];
  for (; i < argc + 4; ++i)
    shell_argv[i-1] = 0;
  shell_argc = argc - 1;

  if ((path = getenv("PATH")) == 0)
    die1(111, "No PATH is set");
  if ((devnull = open("/dev/null", O_RDWR)) == -1)
    die1sys(111, "Could not open \"/dev/null\"");
  if (!nonblock_on(0))
    die1sys(111, "Could not set non-blocking status");
  if ((selfpipe = selfpipe_init()) == -1)
    die1sys(111, "Could not create self-pipe");
  init_slots();
  connection_init(&conn, 0, 0);
  fds[0].fd = 0;
  fds[0].events = IOPOLL_READ;
  fds[1].fd = selfpipe;
  fds[1].events = IOPOLL_READ;
  while (iopoll_restart(fds, 2, -1) != -1) {
    if (fds[0].revents) {
      if (connection_read(&conn, handle_packet) <= 0)
	return 0;
    }
    if (fds[1].revents) {
      read(selfpipe, &i, 1);
      handle_child();
    }
  }
  die1sys(111, "Poll failed");
  return 1;
}
