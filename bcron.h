#ifndef BCRON__H__
#define BCRON__H__

#include <sysdeps.h>
#include <systime.h>
#include <adt/ghash.h>
#include <str/str.h>

#define BCRON_SPOOL "/var/spool/cron"
#define CRONTAB_DIR "crontabs"
#define SOCKET_PATH "/tmp/.bcron-spool"
#define BCRON_USER "cron"
#define TRIGGER "trigger"

#define DEBUG_MISC 1
#define DEBUG_SCHED 2
#define DEBUG_JOBS 4
#define DEBUG_CONN 8

#define IGNORE_EXEC 1

/*****************************************************************************/
extern str tempname;
int tempfile(const char* prefix);

/*****************************************************************************/
const char* fmttm(const struct tm*);
const char* fmttime(time_t t);

/*****************************************************************************/
struct ministat
{
  int exists;
  dev_t device;
  ino_t inode;
  mode_t mode;
  size_t size;
  time_t mtime;
};

void ministat(const char* path, struct ministat* s);
void ministat2(const char* base, const char* entry, struct ministat* s);
void minifstat(int fd, struct ministat* s);

/*****************************************************************************/
struct connection
{
  int fd;
  enum { LENGTH, DATA, COMMA } state;
  unsigned long length;
  str packet;
  void* data;
};

void connection_init(struct connection* c, int fd, void* data);
int connection_read(struct connection* c,
		    void (*handler)(struct connection*));

/*****************************************************************************/
int sendpacket(int fd, const str* s);

/*****************************************************************************/
int chdir_bcron(void);

/*****************************************************************************/
typedef uint64 bitmap;

struct job_timespec
{
  bitmap minutes;		/* bitmap: 0-59 */
  bitmap hours;			/* bitmap: 0-23 */
  bitmap mdays;			/* bitmap: 1-31 */
  bitmap months;		/* bitmap: 0-11 */
  bitmap wdays;			/* bitmap: 0-6 */
  int hour_count;
};

void timespec_next_init(void);
time_t timespec_next(const struct job_timespec* ts,
		     time_t start, const struct tm* starttm);
int timespec_parse(struct job_timespec* t, const char* s);

/*****************************************************************************/
struct job
{
  struct job_timespec times;
  time_t nexttime;
  const char* runas;
  const char* command;
  str environ;
  struct job* next;
};

#define ALL_MINUTES 0xfffffffffffffffULL
#define ALL_HOURS 0xffffffUL
#define ALL_MDAYS 0xfffffffeUL
#define ALL_MONTHS 0xfffUL
#define ALL_WDAYS 0x7fUL

struct job* job_new(const struct job_timespec* times,
		    const char* runas,
		    const char* command,
		    const str* environ);
void job_free(struct job* job);
void job_exec(struct job* job);

/*****************************************************************************/
struct crontab
{
  struct ministat stat;
  struct job* jobs;
};

int crontab_parse(struct crontab* c, str* data, const char* runas);
int crontab_load(struct crontab* c, const char* path, const char* runas);
void crontab_free(struct crontab* c);

/*****************************************************************************/
extern struct ghash crontabs;
GHASH_DECL(crontabs,const char*,struct crontab);
void crontabs_load(void);

#endif
