#include <sysdeps.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iobuf/iobuf.h>
#include <misc/ucspi.h>
#include <msg/msg.h>
#include <path/path.h>
#include <str/str.h>
#include <unix/trigger.h>

#include "bcron.h"

const char program[] = "bcron-spool";
const int msg_show_pid = 1;

static str filename;
static const char* username;

static char** fixup_argv;

static void respond(const char* msg)
{
  obuf_putnetstring(&outbuf, msg, strlen(msg));
  obuf_flush(&outbuf);
  switch (msg[0]) {
  case 'K':
    exit(0);
  case 'Z':
    die3sys(111, username, ": ", msg + 1);
  default:
    die3(100, username, ": ", msg + 1);
  }
}

static void respond_okstr(const str* s)
{
  obuf_putu(&outbuf, s->len + 1);
  obuf_putc(&outbuf, ':');
  obuf_putc(&outbuf, 'K');
  obuf_putstr(&outbuf, s);
  obuf_putc(&outbuf, ',');
  obuf_flush(&outbuf);
  exit(0);
}

static void make_filename(const char* name)
{
  if (!str_copy2s(&filename, CRONTAB_DIR "/", name))
    respond("ZCould not produce filename");
}

static int fixup(int fd)
{
  int pid;
  int status;
  int newfd;
  if (fixup_argv != 0) {
    if (lseek(fd, 0, SEEK_SET) != 0)
      respond("ZCould not seek in temporary file");
    if ((newfd = tempfile("tmp/spool")) == -1)
      respond("ZCould not create temporary file");
    if ((pid = fork()) == -1)
      respond("ZCould not fork fixup program");
    if (pid == 0) {
      dup2(fd, 0);
      close(fd);
      dup2(newfd, 1);
      close(newfd);
      execvp(fixup_argv[0], fixup_argv);
      die3sys(111, "Could not exec '", fixup_argv[0], "'");
    }
    if (waitpid(pid, &status, 0) != pid)
      respond("ZWaitpid failed");
    if (status != 0)
      respond("ZFilter failed");
    fd = newfd;
  }
  return fd;
}

static void cmd_store(str* data)
{
  int i;
  int fd;
  if ((i = str_findfirst(data, 0)) <= 0)
    respond("DStore command is missing data");
  ++i;
  if ((fd = tempfile("tmp/spool")) == -1)
    respond("ZCould not create temporary file");
  if (write(fd, data->s + i, data->len - i) != (long)(data->len - i)
      || (fd = fixup(fd)) == -1
      || fchmod(fd, 0400) == -1
      || close(fd) != 0)
    respond("ZCould not write temporary file");
  if (rename(tempname.s, filename.s) != 0)
    respond("ZCould not rename temporary file");
  trigger_pull(TRIGGER);
  respond("KCrontab successfully written");
}

static void cmd_list(void)
{
  str data = {0,0,0};
  if (ibuf_openreadclose(filename.s, &data) == -1) {
    if (errno == ENOENT)
      respond("DCrontab does not exist");
    else
      respond("ZCould not read crontab");
  }
  respond_okstr(&data);
}

static void cmd_remove(void)
{
  if (unlink(filename.s) != 0 && errno != ENOENT)
    respond("ZCould not remove crontab");
  trigger_pull(TRIGGER);
  respond("KCrontab removed");
}

static void cmd_listsys(void)
{
  DIR* dir;
  direntry* entry;
  str data = {0,0,0};
  if ((dir = opendir(CRONTAB_DIR)) == 0)
    respond("ZCould not open crontabs directory");
  while ((entry = readdir(dir)) != 0) {
    if (entry->d_name[0] != ':')
      continue;
    make_filename(entry->d_name);
    if (!str_cat3s(&data, "==> ", entry->d_name, " <==\n"))
      respond("ZOut of memory");
    if (ibuf_openreadclose(filename.s, &data) == -1)
      respond("ZCould not read crontab");
    if (!str_catc(&data, '\n'))
      respond("ZOut of memory");
  }
  closedir(dir);
  respond_okstr(&data);
}

static void logcmd(char cmd)
{
  char cmdstr[3];
  cmdstr[0] = cmd;
  cmdstr[1] = ' ';
  cmdstr[2] = 0;
  msg2(cmdstr, username);
}

int main(int argc, char* argv[])
{
  str packet = {0,0,0};
  const char* s;
  uid_t euid = -1;
  const struct passwd* pw;

  if (chdir_bcron() != 0)
    respond("ZCould not change directory");

  if (argc > 1)
    fixup_argv = argv + 1;

  if ((s = ucspi_protocol()) == 0
      || (strcmp(s, "UNIX") != 0 && strcmp(s, "LOCAL") != 0)
      || (s = ucspi_getenv("REMOTEEUID")) == 0
      || (euid = strtoul(s, (char**)&s, 0)) == (unsigned)-1
      || *s != 0)
    respond("DConfiguration error: must be run from unixserver");
  if (!ibuf_getnetstring(&inbuf, &packet)
      || packet.len < 2)
    respond("ZInvalid input data or read error");
  /* Look up and validate username */
  username = packet.s + 1;
  if ((pw = getpwnam(username)) == 0)
    respond("DInvalid or unknown username");
  if (euid != 0 && euid != pw->pw_uid)
    respond("DUsername does not match invoking UID");
  if (!str_copy2s(&filename, CRONTAB_DIR "/", pw->pw_name))
    respond("ZCould not produce filename");
  logcmd(packet.s[0]);
  /* Execute the command. */
  switch (packet.s[0]) {
  case 'S': cmd_store(&packet); break;
  case 'L': cmd_list(); break;
  case 'R': cmd_remove(); break;
  case 'Y':
    if (euid != 0 && euid != getuid())
      respond("DOnly root or cron can list system crontabs");
    cmd_listsys();
    break;
  }
  respond("DInvalid command code");
  return 0;
}
