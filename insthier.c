#include <installer.h>
#include "conf_bin.c"

void insthier(void)
{
  int dir = opendir(conf_bin);
  c(dir, "bcron-exec",     -1, -1, 0755);
  c(dir, "bcron-sched",    -1, -1, 0755);
  c(dir, "bcron-spool",    -1, -1, 0755);
  c(dir, "bcron-start",    -1, -1, 0755);
  c(dir, "bcron-update",   -1, -1, 0755);
  c(dir, "bcrontab",       -1, -1, 0755);
  s(dir, "crontab", "bcrontab");
}
