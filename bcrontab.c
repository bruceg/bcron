#include <sysdeps.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cli/cli.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <net/socket.h>
#include <path/path.h>
#include <str/str.h>

#include "bcron.h"

static const char* user = 0;
static int cmd_list = 0;
static int cmd_remove = 0;
static int cmd_edit = 0;
static int cmd_listsys = 0;

const char program[] = "bcrontab";
const int msg_show_pid = 0;
const char cli_help_prefix[] = "\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "[ -u user ] file\n"
"   or: bcrontab [ -u user ] { -l | -r | -e }";
const int cli_args_min = 0;
const int cli_args_max = 1;
cli_option cli_options[] = {
  // { ch, name, type, value, ptr, help, default },
  // type is:
  //  CLI_FLAG,
  //  CLI_COUNTER,
  //  CLI_INTEGER,
  //  CLI_UINTEGER,
  //  CLI_STRING,
  //  CLI_STRINGLIST,
  //  CLI_FUNCTION,
  //  CLI_SEPARATOR,
  /* The following four options are required for compatability */
  { 'u', "user", CLI_STRING, 0, &user,
    "Specify the user name", 0 },
  { 'l', "list", CLI_FLAG, 1, &cmd_list,
    "List user's current crontab", 0 },
  { 'r', "remove", CLI_FLAG, 1, &cmd_remove,
    "Remove user's current crontab", 0 },
  { 'y', "system", CLI_FLAG, 1, &cmd_listsys,
    "List all system crontabs", 0 },
  { 'e', "edit", CLI_FLAG, 1, &cmd_edit,
    "Edit user's current crontab", 0 },
  /* Extended options */
  {0,0,0,0,0,0,0}
};

static int do_connect(void)
{
  const char* sockpath;
  int sock;
  
  if ((sockpath = getenv("BCRON_SOCKET")) == 0)
    sockpath = SOCKET_PATH;
  if ((sock = socket_unixstr()) == -1)
    die1sys(111, "Could not create socket");
  if (!socket_connectu(sock, sockpath))
    die1sys(111, "Could not connect to bcron-spool");
  return sock;
}

static str packet;

static int docmd(char cmd, str* data, int softfail)
{
  int sock;
  ibuf sockbuf;

  packet.len = 0;
  wrap_str(str_catc(&packet, cmd));
  wrap_str(str_cats(&packet, user));
  if (data) {
    wrap_str(str_catc(&packet, 0));
    wrap_str(str_cat(&packet, data));
  }
  sock = do_connect();
  if (!ibuf_init(&sockbuf, sock, 0, IOBUF_NEEDSCLOSE, 0))
    die1sys(111, "Could not initialize socket buffer");
  if (sendpacket(sock, &packet) <= 0)
    die1sys(111, "Could not send command to server");
  if (!ibuf_getnetstring(&sockbuf, &packet))
    die1sys(111, "Could not read response from server");
  ibuf_close(&sockbuf);
  if ((!softfail && packet.s[0] == 'D') || packet.s[0] == 'Z')
    die1(111, packet.s+1);
  return packet.s[0] != 'D';
}

static int do_list(void)
{
  docmd('L', 0, 0);
  return write(1, packet.s + 1, packet.len - 1) != (long)(packet.len - 1);
}

static int do_remove(void)
{
  docmd('R', 0, 0);
  return 0;
}

static int do_replace(const char* path)
{
  static str file;
  if (!ibuf_openreadclose(path, &file))
    die3sys(111, "Could not read '", path, "'");
  docmd('S', &file, 0);
  msg1("crontab saved successfully");
  return 0;
}

static void edit(const char* path)
{
  const char* editor;
  int pid;
  static str cmd;
  char* argv[4];
  int status;
  
  if ((editor = getenv("VISUAL")) == 0
      && (editor = getenv("EDITOR")) == 0)
    editor = "vi";
  if (!str_copys(&cmd, editor)
      || !str_cats(&cmd, " '")
      || !str_cats(&cmd, path)
      || !str_catc(&cmd, '\'')) {
    unlink(path);
    die_oom(111);
  }

  argv[0] = "/bin/sh";
  argv[1] = "-c";
  argv[2] = cmd.s;
  argv[3] = 0;

  if ((pid = fork()) == -1) {
    unlink(path);
    die1sys(111, "Could not fork editor");
  }

  if (pid == 0) {
    /* Run editor in child */
    execv(argv[0], argv);
    die1sys(111, "Could not execute /bin/sh");
  }
  if (waitpid(pid, &status, 0) == -1) {
    unlink(path);
    die1sys(111, "Internal error");
  }
  if (!WIFEXITED(status)) {
    unlink(path);
    die1(111, "Editor crashed");
  }
  if (WEXITSTATUS(status) != 0) {
    unlink(path);
    exit(WEXITSTATUS(status));
  }
}

static int do_edit(void)
{
  str file = {0,0,0};
  int fd;

  if ((fd = tempfile("bcrontab")) == -1
      && (fd = tempfile("/tmp/bcrontab")) == -1)
    die1sys(111, "Could not create temporary file");
  if (!docmd('L', 0, 1))
    wrap_str(str_copys(&packet, "K"));
  if (write(fd, packet.s + 1, packet.len - 1) != (long)(packet.len - 1))
    die1sys(111, "Could not copy crontab to temporary file");
  close(fd);

  edit(tempname.s);

  /* Re-open the temporary file here because some editors will move the
     original file aside before writing the new one. */
  if (!ibuf_openreadclose(tempname.s, &file))
    die1sys(111, "Could not re-read temporary file");

  if (file.len == packet.len - 1
      && memcmp(file.s, packet.s + 1, file.len) == 0)
    msg1("no changes made to crontab");
  else
    docmd('S', &file, 0);
  return 0;
}

static int do_listsys(void)
{
  docmd('Y', 0, 0);
  return write(1, packet.s + 1, packet.len - 1) != (long)(packet.len - 1);
}

int cli_main(int argc, char* argv[])
{
  if (user == 0
      && (user = getenv("LOGNAME")) == 0
      && (user = getenv("USER")) == 0)
    die1(111, "Could not determine user name");

  switch (cmd_list + cmd_remove + cmd_edit + cmd_listsys) {
  case 0:
    if (argc != 1)
      usage(111, "Too few command-line arguments");
    break;
  case 1:
    if (argc != 0)
      usage(111, "Too many command-line arguments");
    break;
  default:
    usage(111, "You must specify only one of -l, -r, -e, or -y");
  }

  if (cmd_list)
    return do_list();
  else if (cmd_remove)
    return do_remove();
  else if (cmd_edit)
    return do_edit();
  else if (cmd_listsys)
    return do_listsys();
  else /* cmd_replace */
    return do_replace(argv[0]);
}
