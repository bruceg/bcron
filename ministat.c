#include <bglibs/sysdeps.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <bglibs/msg.h>
#include <bglibs/wrap.h>
#include <bglibs/str.h>

#include "bcron.h"

static void copystat(struct ministat* m, const struct stat* s)
{
  memset(m, 0, sizeof *m);
  m->exists = 1;
  m->device = s->st_dev;
  m->inode = s->st_ino;
  m->mode = s->st_mode;
  m->size = s->st_size;
  m->mtime = s->st_mtime;
}

void minifstat(int fd, struct ministat* s)
{
  struct stat st;
  if (fstat(fd, &st) != 0)
    die1sys(111, "Could not fstat");
  else
    copystat(s, &st);
}

void ministat(const char* path, struct ministat* s)
{
  struct stat st;
  if (stat(path, &st) != 0) {
    if (errno == ENOENT)
      memset(s, 0, sizeof *s);
    else
      die3sys(111, "Could not stat '", path, "'");
  }
  else
    copystat(s, &st);
}

void ministat2(const char* base, const char* entry, struct ministat* s)
{
  static str path;
  wrap_str(str_copy3s(&path, base, "/", entry));
  ministat(path.s, s);
}
