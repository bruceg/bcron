#include <bglibs/sysdeps.h>
#include <bglibs/systime.h>
#include <stdlib.h>
#include <string.h>

#include <bglibs/cli.h>
#include <bglibs/iobuf.h>
#include <bglibs/misc.h>
#include <bglibs/msg.h>

#include "bcron.h"

const char program[] = "test-sched";
const int msg_show_pid = 0;
int msg_debug_bits = 0;
const char cli_help_prefix[] = "\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "cron-line time";
const int cli_args_min = 2;
const int cli_args_max = 2;
cli_option cli_options[] = {
  {0,0,0,0,0,0,0}
};

int cli_main(int argc, char* argv[])
{
  time_t start;
  time_t next;
  struct crontab c;
  str jobstr;

  msg_debug_init();
  timespec_next_init();

  memset(&c, 0, sizeof c);
  jobstr.len = strlen(jobstr.s = argv[0]);
  jobstr.size = 0;
  if (!crontab_parse(&c, &jobstr, "nobody")
      || c.jobs == 0)
    usage(111, "Invalid crontab line");

  if ((start = strtol(argv[1], 0, 10)) <= 0)
    usage(111, "Invalid timesstamp");

  next = timespec_next(&c.jobs->times, start, localtime(&start));

  obuf_put5s(&outbuf, "last: ", utoa(start), " ", fmttime(start), "\n");
  obuf_put5s(&outbuf, "next: ", utoa(next), " ", fmttime(next), "\n");
  obuf_flush(&outbuf);

  return 0;
  (void)argc;
}
