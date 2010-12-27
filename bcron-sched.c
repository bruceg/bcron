#include <sysdeps.h>
#include <systime.h>
#include <errno.h>
#include <unistd.h>

#include <iobuf/iobuf.h>
#include <misc/misc.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <str/iter.h>
#include <str/str.h>
#include <unix/nonblock.h>
#include <unix/trigger.h>

#include "bcron.h"

const char program[] = "bcron-sched";
const int msg_show_pid = 0;
int msg_debug_bits = 0;

static iopoll_fd ios[3];

/* Fill in tm and calculate the top of the next minute. */
static time_t next_minute(time_t t, struct tm* tm)
{
  t += 60;
  *tm = *(localtime(&t));
  t -= tm->tm_sec;
  tm->tm_sec = 0;
  return t;
}

static time_t nexttime;
static struct tm nexttm;
static struct timeval reftime;

static void calctimes(void)
{
  struct ghashiter i;
  struct job* job;

  nexttime = next_minute(reftime.tv_sec, &nexttm);

  /* Make sure all jobs have a time scheduled */
  ghashiter_loop(&i, &crontabs) {
    for (job = ((struct crontabs_entry*)i.entry)->data.jobs;
	 job != 0; job = job->next) {
      if (job->nexttime == 0)
	job->nexttime = timespec_next(&job->times, nexttime, &nexttm);
    }
  }
}

static void loadall(void)
{
  gettimeofday(&reftime, 0);
  crontabs_load();
  calctimes();
}

static long calcinterval(void)
{
  long interval;
  gettimeofday(&reftime, 0);
  interval = (nexttime - reftime.tv_sec) * 1000 - reftime.tv_usec / 1000;
  debug2(DEBUG_SCHED, "Interval: ", utoa(interval));
  /* Add a little "fudge" to the interval, as the select frequently
     returns a jiffy before the interval would actually expire. */
  return interval + 100;
}

static void run_jobs(void)
{
  struct ghashiter i;

  gettimeofday(&reftime, 0);
  if (reftime.tv_sec < nexttime) {
    debug1(DEBUG_SCHED, "Not enough time has elapsed, sleeping again");
    return;
  }

  debug2(DEBUG_SCHED, "Running jobs at: ", fmttime(reftime.tv_sec));
  ghashiter_loop(&i, &crontabs) {
    struct job* job;
    for (job = ((struct crontabs_entry*)i.entry)->data.jobs;
	 job != 0; job = job->next)
      if (job->nexttime <= reftime.tv_sec) {
	job_exec(job);
	job->nexttime = 0;
      }
  }

  loadall();
}

#ifndef IGNORE_EXEC
static void handle_stdin(void)
{
  char buf[256];
  read(0, buf, sizeof buf);
}
#endif

int main(void)
{
  msg_debug_init();
  if (chdir_bcron() != 0)
    die1sys(111, "Could not change directory");
  nonblock_on(0);
  timespec_next_init();
  trigger_set(ios, TRIGGER);

#ifndef IGNORE_EXEC
  ios[2].fd = 0;
  ios[2].events = IOPOLL_READ;
#endif

  msg1("Starting");
  crontabs_init(&crontabs);
  loadall();

  for (;;) {
    switch (iopoll(ios, 3, calcinterval())) {
    case -1:
      if (errno == EAGAIN || errno == EINTR)
	continue;
      die1sys(111, "poll failed");
    case 0:
      run_jobs();
      continue;
    default:
      if (trigger_pulled(ios)) {
	trigger_set(ios, TRIGGER);
	loadall();
      }
#ifndef IGNORE_EXEC
      if (ios[2].revents)
	handle_stdin();
#endif
    }
  }
}
