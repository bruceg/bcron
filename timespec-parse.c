#include <bglibs/sysdeps.h>

#include <ctype.h>
#include <string.h>

#include "bcron.h"

static int parse_value(const char* s, int* out,
		       const char** table)
{
  int v;
  int i;
  if (!isdigit(*s)) {
    if (table != 0) {
      for (i = 0; table[i] != 0; ++i) {
	int len;
	if ((len = strlen(table[i])) == 0)
	  continue;
	if (strncasecmp(table[i], s, len) == 0
	    && !isalpha(s[len])) {
	  *out = i;
	  return len;
	}
	if (strncasecmp(table[i], s, 3) == 0
	    && !isalpha(s[3])) {
	  *out = i;
	  return 3;
	}
      }
    }
    return 0;
  }
  for (i = v = 0; isdigit(s[i]); ++i)
    v = (v * 10) + (s[i] - '0');
  *out = v;
  return i;
}

static int parse_field(const char* s, bitmap* out,
		       int min, int max, bitmap all,
		       const char** table, int numoffset)
{
  int start;
  int i;
  int startbit;
  int endbit;
  int bit;
  int step;
  bitmap bits;
  if (s[0] == '*' && isspace(s[1])) {
    *out = all;
    return 1;
  }
  bits = i = 0;
  do {
    start = i;
    step = 1;
    if (s[i] == '*') {
      ++i;
      startbit = min;
      endbit = max;
    }
    else {
      if ((i += parse_value(s+i, &startbit, table)) == start)
	return 0;
      startbit -= numoffset;
      endbit = startbit;
      if (s[i] == '-') {
	++i;
	start = i;
	if ((i += parse_value(s+i, &endbit, table)) == start)
	  return 0;
	endbit -= numoffset;
      }
    }
    if (s[i] == '/') {
      ++i;
      start = i;
      if ((i += parse_value(s+i, &step, 0)) == start)
	return 0;
    }
    if (startbit > endbit
	|| startbit < min
	|| endbit > max)
      return 0;
    bit = startbit;
    do {
      bits |= 1ULL << bit;
      bit += step;
    } while (bit <= endbit);
    while (s[i] == ',')
      ++i;
  } while (!isspace(s[i]));
  *out = bits;
  return i;
}

static const char* wday_table[] = {
  "sunday",
  "monday",
  "tuesday",
  "wednesday",
  "thursday",
  "friday",
  "saturday",
  "sunday",
  0
};

static const char* month_table[] = {
  "",
  "january",
  "february",
  "march",
  "april",
  "may",
  "june",
  "july",
  "august",
  "september",
  "october",
  "november",
  "december",
  0
};

int timespec_parse(struct job_timespec* t, const char* s)
{
  int i;
  bitmap b;
  const char* start;
  start = s;
  if ((i = parse_field(s, &t->minutes, 0, 59, ALL_MINUTES, 0, 0)) <= 0
      || !isspace(s[i]))
    return 0;
  for (s += i; isspace(*s); ++s) ;
  if ((i = parse_field(s, &t->hours, 0, 23, ALL_HOURS, 0, 0)) <= 0
      || !isspace(s[i]))
    return 0;
  for (s += i; isspace(*s); ++s) ;
  if ((i = parse_field(s, &t->mdays, 1, 31, ALL_MDAYS, 0, 0)) <= 0
      || !isspace(s[i]))
    return 0;
  for (s += i; isspace(*s); ++s) ;
  if ((i = parse_field(s, &t->months, 0, 11, ALL_MONTHS, month_table, 1)) <= 0
      || !isspace(s[i]))
    return 0;
  for (s += i; isspace(*s); ++s) ;
  if ((i = parse_field(s, &t->wdays, 0, 7, ALL_WDAYS, wday_table, 0)) <= 0
      || !isspace(s[i]))
    return 0;
  s += i;
  if (t->wdays & (1 << 7)) {
    t->wdays |= 1;
    t->wdays &= ALL_WDAYS;
  }
  for (b = t->hours, i = 0; b != 0; b >>= 1)
    i += (unsigned)b & 1;
  t->hour_count = i;
  return s - start;
}
