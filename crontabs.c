#include <sysdeps.h>
#include <string.h>

#include <adt/ghash.h>
#include <msg/msg.h>
#include <msg/wrap.h>

#include "bcron.h"

static void crontabp_free(struct crontab* c)
{
  job_free(c->jobs);
}

GHASH_DEFN(crontabs,const char*,struct crontab,
	   adt_hashsp,adt_cmpsp,adt_copysp,0,adt_freesp,crontabp_free);

struct ghash crontabs;

static str path;

static void unload(const char* name)
{
  msg3("Unloading '", name, "'");
  crontabs_remove(&crontabs, &name);
}

static void reload(struct crontabs_entry* entry)
{
  const char* name = entry->key;
  msg3("Reloading '", name, "'");
  wrap_str(str_copy2s(&path, CRONTAB_DIR "/", name));
  if (!crontab_load(&entry->data, path.s, name[0] == ':' ? 0 : name))
    warn3("Reloading '", name, "' failed, using old table");
}

static void load(const char* name)
{
  struct crontab c;
  msg3("Loading '", name, "'");
  wrap_str(str_copy2s(&path, CRONTAB_DIR "/", name));
  memset(&c, 0, sizeof c);
  if (crontab_load(&c, path.s, name[0] == ':' ? 0 : name)) {
    if (!crontabs_add(&crontabs, &name, &c))
      die_oom(111);
  }
  else
    warn3("Loading '", name, "' failed");
}

void crontabs_load(void)
{
  DIR* dir;
  const direntry* de;
  struct crontabs_entry* entry;
  struct ghashiter iter;
  const char* name;
  struct ministat st;

  if ((dir = opendir(CRONTAB_DIR)) == 0)
    die1sys(111, "Could not open crontabs directory");
  while ((de = readdir(dir)) != 0) {
    name = de->d_name;
    if (name[0] != '.') {
      if ((entry = crontabs_get(&crontabs, &name)) == 0)
	load(name);
    }
  }
  closedir(dir);

  ghashiter_loop(&iter, &crontabs) {
    entry = iter.entry;
    ministat2(CRONTAB_DIR, entry->key, &st);
    if (!st.exists)
      unload(entry->key);
    else if (memcmp(&st, &entry->data.stat, sizeof st) != 0)
      reload(entry);
  }
}
