#include <sysdeps.h>
#include <ctype.h>
#include <pwd.h>
#include <string.h>

#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <str/env.h>
#include <str/iter.h>
#include <str/str.h>

#include "bcron.h"


static int parse_assign(str* line, str* env)
{
  unsigned i;
  unsigned nameend;
  for (i = 0; i < line->len; ++i)
    if (isspace(line->s[i]) || line->s[i] == '=')
      break;
  if (i >= line->len)
    return 0;
  nameend = i;
  while (i < line->len && isspace(line->s[i]))
    ++i;
  if (i >= line->len || line->s[i] != '=')
    return 0;
  ++i;
  while (i < line->len && isspace(line->s[i]))
    ++i;
  if (i >= line->len)
    return 0;
  if ((line->s[i] == '"' || line->s[i] == '\'')
      && line->s[line->len - 1] == line->s[i]) {
    ++i;
    line->s[line->len - 1] = 0;
  }
  line->s[nameend] = 0;
  wrap_str(envstr_set(env, line->s, line->s + i, 1));
  return 1;
}

static struct job* parse_job(str* line,
			     const char* runas,
			     const str* environ)
{
  struct job_timespec times;
  int i;
  char* s;
  
  s = line->s;
  if ((i = timespec_parse(&times, s)) == 0
      || !isspace(s[i]))
    return 0;
  for (s += i; isspace(*s); ++s) ;
  if (!runas) {
    runas = s;
    while (*s != 0 && !isspace(*s)) ++s;
    if (s == runas)
      return 0;
    *s++ = 0;
    while (*s != 0 && isspace(*s)) ++s;
    if (getpwnam(runas) == 0) {
      warn3("Unknown user: '", runas, "'");
      return 0;
    }
  }
  if (*s == 0)
    return 0;
  return job_new(&times, runas, s, environ);
}

int crontab_parse(struct crontab* c, str* data, const char* runas)
{
  static str line;
  static str env;
  struct job* job;
  struct job* head = 0;
  struct job* tail = 0;
  striter iter;
  env.len = 0;
  striter_loop(&iter, data, '\n') {
    wrap_str(str_copyb(&line, iter.startptr, iter.len));
    str_strip(&line);
    if (line.len == 0
	|| line.s[0] == '#')
      continue;
    if (parse_assign(&line, &env))
      continue;
    if ((job = parse_job(&line, runas, &env)) != 0) {
      job->next = 0;
      job->nexttime = 0;
      if (head == 0)
	head = job;
      else
	tail->next = job;
      tail = job;
    }
    else
      warn3("Ignoring unparseable line: '", line.s, "'");
  }
  c->jobs = head;
  return 1;
}

int crontab_import(struct crontab* c, const char* path, const char* runas)
{
  static str file;
  struct ibuf in;
  if (c->jobs) {
    job_free(c->jobs);
    c->jobs = 0;
  }
  if (!ibuf_open(&in, path, 0)) {
    warn3sys("Could not open '", path, "'");
    return 0;
  }
  minifstat(in.io.fd, &c->stat);
  file.len = 0;
  if (!ibuf_readall(&in, &file)) {
    warn3sys("Could not read '", path, "'");
    ibuf_close(&in);
    return 0;
  }
  ibuf_close(&in);
  if (!crontab_parse(c, &file, runas)) {
    warn3("Could not parse '", path, "'");
    return 0;
  }
  return 1;
}

void crontab_free(struct crontab* c)
{
  job_free(c->jobs);
  free(c);
}
