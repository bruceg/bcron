#include <systime.h>
#include <string.h>
#include "bcron.h"

const char* fmttm(const struct tm* tm)
{
  static char buf[30];
  buf[strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", tm)] = 0;
  return buf;
}

const char* fmttime(time_t t)
{
  return fmttm(localtime(&t));
}

#if 0
static time_t mktime_internal(int year, int month, int mday,
			      int hour, int minute, int dst)
{
  struct tm tm;
  if (year > 1900)
    year -= 1900;
  printf("mktime_internal(%d,%d,%d,%d,%d,%d)\n",
	 year,month,mday,hour,minute,dst);
  memset(&tm, 0, sizeof tm);
  tm.tm_min = minute;
  tm.tm_hour = hour;
  tm.tm_mday = mday;
  tm.tm_mon = month;
  tm.tm_year = year;
  tm.tm_isdst = dst;
  /* Another stupid: mktime can alter the contents of its parameter,
     so none of the contents can be trusted to have retained their value. */
  return mktime(&tm);
}

time_t mktimesep(int year, int month, int mday, int hour, int minute,
		 int* dst)
{
  time_t t;
  const struct tm* lt;

  /* Try with DST set first: If the time zone doesn't do DST changes,
     the DST flag is ignored.  If the time zone does do DST changes,
     this will cause mktime to select the first part of the skipped
     hour.  Did I ever mention how much I hate DST? */
  t = mktime_internal(year, month, mday, hour, minute, *dst);
  /* Stupid mktime interface requires IN ADVANCE knowledge of the state
     of DST, which is almost impossible without doing the reverse
     conversion (localtime) first. */
  lt = localtime(&t);
  if (hour != lt->tm_hour
      || *dst != lt->tm_isdst) {
    printf("mktimesep retry\n");
    *dst = !*dst;
    t = mktime_internal(year, month, mday, hour, minute, *dst);
  }
  return t;
}
#endif
