#include <sysdeps.h>
#include <systime.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include <msg/msg.h>
#include <msg/wrap.h>
#include <str/env.h>
#include <str/iter.h>
#include <str/str.h>

#include "bcron.h"

static int days_in_month(int month, int year)
{
  switch (month) {
  case 1:			/* February is special */
    return ((year % 4) != 0) ? 28 :
      ((year % 100) != 0) ? 29 :
      ((year % 400) != 0) ? 28 :
      29;
  case 3:			/* April */
  case 5:			/* June */
  case 8:			/* September */
  case 10:			/* November */
    return 30;
  default:			/* All the others have 31 */
    return 31;
  }
}

static time_t fixup_hour(time_t t, struct tm* tm)
{
  *tm = *(localtime(&t));
  /* From 13..23: The hour underflowed */
  if (tm->tm_hour > 12) {
    t += (24 - tm->tm_hour) * 60 * 60;
    return fixup_hour(t, tm);
  }
  /* From 0..12: The hour overflowed */
  else if (tm->tm_hour > 0) {
    t -= tm->tm_hour * 60 * 60;
    tm->tm_hour = 0;
  }
  return t;
}

static void advance_tmmonth(struct tm* tm)
{
  tm->tm_sec = tm->tm_min = tm->tm_hour = 0;
  tm->tm_wday = (tm->tm_wday
		 + days_in_month(tm->tm_mon, tm->tm_year)
		 - (tm->tm_mday - 1)) % 7;
  tm->tm_mday = 1;
  if (++tm->tm_mon >= 12) {
    tm->tm_mon = 0;
    ++tm->tm_year;
  }
}

static void advance_tmday(struct tm* tm)
{
  tm->tm_sec = tm->tm_min = tm->tm_hour = 0;
  tm->tm_wday = (tm->tm_wday + 1) % 7;
  if (++tm->tm_mday > days_in_month(tm->tm_mon, tm->tm_year))
    advance_tmmonth(tm);
}

static void advance_tmhour(struct tm* tm)
{
  tm->tm_sec = tm->tm_min = 0;
  if (++tm->tm_hour >= 24)
    advance_tmday(tm);
}

static time_t advance_month(time_t t, struct tm* tm)
{
  t += (days_in_month(tm->tm_mon, tm->tm_year) - (tm->tm_mday - 1)) * 24*60*60
    - tm->tm_hour * 60*60
    - tm->tm_min*60
    - tm->tm_sec;
  if (daylight)
    t = fixup_hour(t, tm);
  else
    advance_tmmonth(tm);
  return t;
}

static time_t advance_day(time_t t, struct tm* tm)
{
  t += 24*60*60 - tm->tm_hour*60*60 - tm->tm_min*60 - tm->tm_sec;
  if (daylight)
    t = fixup_hour(t, tm);
  else
    advance_tmday(tm);
  return t;
}

static time_t advance_hour(time_t t, struct tm* tm, int daily)
{
  const int prevhour = tm->tm_hour;
  const int wasisdst = tm->tm_isdst;
  t += 60*60 - tm->tm_min * 60 - tm->tm_sec;
  if (daylight) {
    *tm = *(localtime(&t));
    /* Run the skipped hour twice.
     *
     * case1 example: entering repeated hour
     * before: 1081058400 2004-04-04 01:00:00 EST
     * after:  1081062000 2004-04-04 03:00:00 EDT
     * want:   1081062000 2004-04-04 02:00:00 EDT
     *
     * case2 example: exiting repeated hour
     * before: 1081062000 2004-04-04 02:00:00 EDT
     * after:  1081065600 2004-04-04 04:00:00 EDT
     * want:   1081062000 2004-04-04 03:00:00 EDT
     */
    if (tm->tm_hour > prevhour + 1) {
      tm->tm_hour = prevhour + 1;
      if (wasisdst)
	t -= 60*60;
    }
    else if (daily && tm->tm_hour == prevhour)
      return advance_hour(t, tm, 0);
  }
  else
    advance_tmhour(tm);
  return t;
}

static time_t advance_minute(time_t t, struct tm* tm, int daily)
{
  t += 60 - tm->tm_sec;
  tm->tm_sec = 0;
  if (++tm->tm_min >= 60)
    t = advance_hour(t, tm, daily);
  return t;
}

/* daylight is set when tzset(3) is called, and is true if DST applies
   during some part of the year. */
extern int daylight;

/** Calculate the next time this job should get run.

The algorithm used here is very simple: start at the last time the job
was run (or the current time if it hasn't been run yet), and scan
forward effectively minute-by-minute until a time that matches the
specified times is found.  The scan is intentionally limited to one
year, since no job can be specified more than that much in the future.

Obviously, scanning every minute for a year for every job is far too
much work, so the scan is optimized by advancing over larger intervals
if no job is possible during that interval.  For example, if a job is
specified to run only on the 1st of the month, the loop advances whole
days at a time until the month day equals 1.  This means the loop will
complete in a maximum of 113 steps (12-1 months + 31-1 days + 24-1 hours
+ 60-1 minutes).  Also, the more frequently a job needs to be scheduled,
the fewer steps are required to calculate its next time.

My first attempt at this loop calculated exclusively using broken down
time.  However, using that method, it was impossible to both (1) prevent
jobs from running twice on the duplicated DST hour and (2) still run
jobs that are supposed to run during the duplicated DST hour.
*/
time_t timespec_next(const struct job_timespec* ts,
		     time_t start, const struct tm* starttm)
{
  time_t t;
  struct tm tm;
  time_t timelimit;
  const int daily = ts->hour_count <= 1;

  tm = *starttm;
  t = start - tm.tm_sec;
  tm.tm_sec = 0;
  timelimit = start + 366*24*60*60 * 10;

  do {
    assert(tm.tm_sec == 0);

    if ((ts->months & (1ULL << tm.tm_mon)) != 0)

      if ((ts->mdays & (1ULL << tm.tm_mday)) != 0
	  && (ts->wdays & (1ULL << tm.tm_wday)) != 0)

	if ((ts->hours & (1ULL << tm.tm_hour)) != 0)

	  if ((ts->minutes & (1ULL << tm.tm_min)) != 0)
	    return t;

	  else
	    t = advance_minute(t, &tm, daily);
	else {
	  t = advance_hour(t, &tm, daily);
	}
      else
	t = advance_day(t, &tm);
    else
      t = advance_month(t, &tm);

  } while (t < timelimit);
  return INT_MAX;
}

void timespec_next_init(void)
{
  /* This sets daylight, which is used in calculating times. */
  tzset();
  debug4(DEBUG_SCHED, "Timezone: ", tzname[0], " alt: ", tzname[1]);
}
