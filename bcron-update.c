#include <sysdeps.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <adt/ghash.h>
#include <msg/msg.h>
#include <msg/wrap.h>
#include <path/path.h>
#include <str/iter.h>
#include <str/str.h>
#include <unix/trigger.h>

#include "bcron.h"

const char program[] = "bcron-update";
const int msg_show_pid = 0;

static const struct passwd* cronpw;

/*****************************************************************************/
GHASH_DECL(statcache,const char*,struct ministat);
GHASH_DEFN(statcache,const char*,struct ministat,
	   adt_hashsp,adt_cmpsp,adt_copysp,0,adt_freesp,0);

/*****************************************************************************/
static void copy(const char* pathin,
		 const char* pathout)
{
  int fdin;
  int fdout;
  char buf[4096];
  long rd;
  if ((fdin = open(pathin, O_RDONLY)) == -1) {
    if (errno == ENOENT)
      /* File disappeared, ignore */
      return;
    die3sys(111, "Could not open '", pathin, "'");
  }
  if ((fdout = tempfile("tmp/")) == -1)
    die1sys(111, "Could not create temporary file");
  while ((rd = read(fdin, buf, sizeof buf)) > 0) {
    if (write(fdout, buf, rd) != rd)
      die3sys(111, "Could not write '", tempname.s, "'");
  }
  if (rd < 0)
    die3sys(111, "Could not read '", pathin, "'");
  close(fdin);
  if (fchown(fdout, cronpw->pw_uid, cronpw->pw_gid) != 0)
    die3sys(111, "Could not change ownership on '", tempname.s, "'");
  if (fchmod(fdout, 0400) != 0)
    die3sys(111, "Could not change permissions on '", tempname.s, "'");
  if (close(fdout) != 0)
    die3sys(111, "Could not write '", tempname.s, "'");
  if (rename(tempname.s, pathout) != 0)
    die5sys(111, "Could not rename '", tempname.s, "' to '", pathout, "'");
  tempname.len = 0;
}

static str path;
static str dstpath;
static str tmp;

static int actions;

static void act(char a, const char* s1, const char* s2)
{
  wrap_str(str_copys(&path, s1));
  if (s2 != 0) {
    wrap_str(str_catc(&path, '/'));
    wrap_str(str_cats(&path, s2));
  }
  wrap_str(str_copy(&tmp, &path));
  str_subst(&tmp, '/', ':');
  wrap_str(str_copys(&dstpath, CRONTAB_DIR "/"));
  wrap_str(str_cat(&dstpath, &tmp));
  
  switch (a) {
  case '-':
    msg2("-", path.s);
    unlink(dstpath.s);
    break;
  default:
    msg2("+", path.s);
    copy(path.s, dstpath.s);
  }
  actions = 1;
}

/*****************************************************************************/
struct arg
{
  const char* path;
  DIR* dir;
  struct ghash entries;
  struct ministat st;
};

static struct arg* args;

/*****************************************************************************/
static int check_file(struct arg* a, struct ministat* st)
{
  if (!st->exists) {
    if (a->st.exists) {
      act('-', a->path, 0);
      a->st.exists = 0;
      return 1;
    }
  }
  else {
    if (memcmp(&a->st, st, sizeof *st) != 0) {
      act(a->st.exists ? '*' : '+', a->path, 0);
      a->st = *st;
      return 1;
    }
  }
  return 0;
}

static int check_dir(struct arg* a, struct ministat* st)
{
  int count;
  struct ghashiter i;
  direntry* e;
  count = 0;
  /* Only need to scan for new files if the directory has changed. */
  if (memcmp(&a->st, st, sizeof *st) != 0) {
    memcpy(&a->st, st, sizeof *st);
    msg2("Rescanning ", a->path);
    rewinddir(a->dir);
    while ((e = readdir(a->dir)) != 0) {
      struct statcache_entry* se;
      const char* name = e->d_name;
      if (name[0] == '.')
	continue;
      if ((se = statcache_get(&a->entries, &name)) == 0) {
	/* File is new. */
	ministat2(a->path, name, st);
	if (S_ISREG(st->mode)) {
	  act('+', a->path, name);
	  if (!statcache_add(&a->entries, &name, st))
	    die_oom(111);
	}
      }
    }
  }
  ghashiter_loop(&i, &a->entries) {
    struct statcache_entry* se = i.entry;
    ministat2(a->path, se->key, st);
    if (st->exists) {
      if (memcmp(&se->data, st, sizeof *st) != 0
	  && S_ISREG(st->mode)) {
	memcpy(&se->data, st, sizeof *st);
	act('*', a->path, se->key);
      }
    }
    else {
      act('-', a->path, se->key);
      statcache_remove(&a->entries, &se->key);
    }
  } 
  return count;
}

static void checkdirs(int argc)
{
  int i;
  struct arg* a;
  struct ministat st;
  actions = 0;
  for (i = 0, a = args; i < argc; ++i, ++a) {
    ministat(a->path, &st);
    if (a->dir)
      check_dir(a, &st);
    else
      check_file(a, &st);
  }
  if (actions)
    trigger_pull(TRIGGER);
}

static void opendirs(int argc, char* argv[])
{
  int i;
  struct ministat st;
  /* Make sure all filenames are absolute. */
  for (i = 0; i < argc; ++i) {
    if (argv[i][0] != '/')
      die1(111, "All filenames must be absolute");
  }
  /* Setup the argument array. */
  if ((args = malloc(argc * sizeof *args)) == 0)
    die_oom(111);
  memset(args, 0, argc * sizeof *args);
  for (i = 0; i < argc; ++i) {
    args[i].path = argv[i];
    ministat(argv[i], &st);
    if (!st.exists)
      die3(111, "Path does not exist: '", argv[i], "'");
    if (S_ISDIR(st.mode)) {
      if ((args[i].dir = opendir(argv[i])) == 0)
	die3sys(111, "Could not open directory '", argv[i], "'");
      statcache_init(&args[i].entries);
    }
    else
      args[i].dir = 0;
  }
}

/* Purge all the entries in the crontabs directory generated by this
 * program.
 */
static void purge(int argc, char* argv[])
{
  DIR* dir;
  direntry* e;
  int i;
  if ((dir = opendir(CRONTAB_DIR)) == 0)
    die1sys(111, "Could not open directory '" CRONTAB_DIR "'");
  for (i = 0; i < argc; ++i) {
    wrap_str(str_copys(&tmp, argv[i]));
    str_subst(&tmp, '/', ':');
    rewinddir(dir);
    while ((e = readdir(dir)) != 0) {
      if (memcmp(e->d_name, tmp.s, tmp.len) == 0 &&
	  (e->d_name[tmp.len] == 0 || e->d_name[tmp.len] == ':')) {
	wrap_str(str_copy2s(&path, CRONTAB_DIR "/", e->d_name));
	unlink(path.s);
      }
    }
  }
  closedir(dir);
}

int main(int argc, char* argv[])
{
  const char* user;
  if ((user = getenv("BCRON_USER")) == 0)
    user = BCRON_USER;
  if ((cronpw = getpwnam(user)) == 0)
    die1(111, "Could not look up cron user");

  /* Make sure there are files to check. */
  if (argc < 2)
    die1(111,
	 "Must give at least one filename or directory on the command-line");
  opendirs(argc - 1, argv + 1);
  if (chdir_bcron() != 0)
    die1sys(111, "Could not change directory");
  purge(argc - 1, argv + 1);
  for(;;) {
    checkdirs(argc - 1);
    iopoll(0, 0, 60 * 1000);
  }
}
