#include <bglibs/sysdeps.h>
#include <bglibs/systime.h>
#include <ctype.h>
#include <string.h>

#include <bglibs/msg.h>
#include <bglibs/wrap.h>
#include <bglibs/envstr.h>
#include <bglibs/striter.h>
#include <bglibs/str.h>

#include "bcron.h"

void job_free(struct job* job)
{
  if (job) {
    job_free(job->next);
    if (job->runas != 0)
      free((char*)job->runas);
    if (job->command != 0)
      free((char*)job->command);
    str_free(&job->environ);
    free(job);
  }
}

struct job* job_new(const struct job_timespec* times,
		    const char* runas,
		    const char* command,
		    const str* environ)
{
  struct job* this;
  if ((this = malloc(sizeof *this)) == 0)
    die_oom(111);
  memset(this, 0, sizeof *this);
  memcpy(&this->times, times, sizeof *times);
  if ((runas && (this->runas = strdup(runas)) == 0)
      || (this->command = strdup(command)) == 0
      || !str_copy(&this->environ, environ))
    die_oom(111);
  return this;
}

void job_exec(struct job* job)
{
  static str packet;
  static unsigned long id = 1;
  const char *runas = job->runas != 0 ? job->runas : "";
  debugf(DEBUG_JOBS, "{Running: (}s{) }s", runas, job->command);
  packet.len = 0;
  str_catu(&packet, id++);
  str_catc(&packet, 0);
  str_catb(&packet, runas, strlen(runas) + 1);
  str_catb(&packet, job->command, strlen(job->command) + 1);
  str_cat(&packet, &job->environ);
  if (sendpacket(1, &packet) == -1)
    die1sys(111, "Could not send job to bcron-exec");
}
