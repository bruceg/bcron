#include <sys/time.h>
#include <stdlib.h>
#include <syscall.h>
#include <time.h>

static void startup(void) __attribute__ ((constructor));

static long offset = 0;

static void startup(void) {
  const char* env;
  long shiftime;
  struct timeval t;

  /* Calculate the initial offset */
  if ((env = getenv("SHIFTIME")) != 0
      && (shiftime = strtol(env, 0, 10)) > 0) {
    syscall(SYS_gettimeofday, &t, 0);
    offset = shiftime - t.tv_sec;
    fprintf(stderr, "offset set to %ld\n", offset);
  }
}

int gettimeofday(struct timeval* tv, struct timezone* tz)
{
  int i;
  if ((i = syscall(SYS_gettimeofday, tv, tz)) != 0)
    return i;
  tv->tv_sec += offset;
  return 0;
}

time_t time(time_t* ptr)
{
  struct timeval t;
  gettimeofday(&t, 0);
  if (ptr != 0)
    *ptr = t.tv_sec;
  return t.tv_sec;
}
