#include <sysdeps.h>
#include <stdlib.h>
#include <unistd.h>
#include "bcron.h"

int chdir_bcron(void)
{
  const char* dir;
  if ((dir = getenv("BCRON_SPOOL")) == 0)
    dir = BCRON_SPOOL;
  return chdir(dir);
}
