#include <bglibs/sysdeps.h>

#include <bglibs/msg.h>
#include <bglibs/wrap.h>
#include <bglibs/str.h>

#include "bcron.h"

static int str_cat_range(str *s, int comma, int start, int end)
{
  if (comma)
    if (!str_catc(s, ','))
      return 0;
  if (start == end)
    return str_cati(s, start);
  else
    return str_catf(s, "i\\-i", start, end);
}

static int str_cat_bitmap(str *s, bitmap bits)
{
  int i;
  int start = -1;
  int comma = 0;

  for (i = 0; i < 64; ++i, bits >>= 1) {
    if (bits & 1) {
      if (start < 0)
	start = i;
    }
    else
      if (start >= 0) {
	if (!str_cat_range(s, comma, start, i-1))
	  return 0;
	start = -1;
	comma = 1;
      }
  }
  return (start >= 0)
    ? str_cat_range(s, comma, start, i-1)
    : 1;
}

void job_dump(struct job* job)
{
  static str line;

  wrap_str(str_copys(&line, "M:"));
  wrap_str(str_cat_bitmap(&line, job->times.minutes));
  wrap_str(str_cats(&line, " H:"));
  wrap_str(str_cat_bitmap(&line, job->times.hours));
  wrap_str(str_cats(&line, " d:"));
  wrap_str(str_cat_bitmap(&line, job->times.mdays));
  wrap_str(str_cats(&line, " m:"));
  wrap_str(str_cat_bitmap(&line, job->times.months));
  wrap_str(str_cats(&line, " wd:"));
  wrap_str(str_cat_bitmap(&line, job->times.wdays));

  wrap_str(str_catf(&line, "{ hc:}i", job->times.hour_count));

  wrap_str(str_catf(&line, "{ next:}s{ runas:(}s{) cmd:(}s{)}",
		    fmttime(job->nexttime),
		    job->runas ? job->runas : "",
		    job->command));
  msg1(line.s);
}
