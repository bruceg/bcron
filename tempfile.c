#include <sysdeps.h>
#include <unistd.h>

#include <path/path.h>

#include "bcron.h"

str tempname = {0,0,0};

static void cleanup(void)
{
  if (tempname.len > 0)
    unlink(tempname.s);
}

int tempfile(const char* prefix)
{
  if (tempname.s == 0)
    atexit(cleanup);
  else
    cleanup();
  return path_mktemp(prefix, &tempname);
}
