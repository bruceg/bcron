#include <sysdeps.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <str/str.h>
#include <unix/nonblock.h>
#include "bcron.h"

void connection_init(struct connection* c, int fd, void* data)
{
  memset(c, 0, sizeof *c);
  c->fd = fd;
  c->state = LENGTH;
  c->data = data;
  nonblock_on(fd);
}

int connection_read(struct connection* c,
		    void (*handler)(struct connection*))
{
  static char buf[512];
  long rd;
  long i;
  if ((rd = read(c->fd, buf, sizeof buf)) <= 0)
    return rd;
  for (i = 0; i < rd; ) {
    /* Optimize packet appends */
    if (c->state == DATA) {
      long todo = rd - i;
      if (c->packet.len + todo >= c->length) {
	todo = c->length - c->packet.len;
	c->state = COMMA;
      }
      str_catb(&c->packet, buf + i, todo);
      i += todo;
    }
    else {
      const char ch = buf[i++];
      switch (c->state) {
      case LENGTH:
	if (ch == ':') {
	  if (!str_ready(&c->packet, c->length)) {
	    errno = ENOMEM;
	    return -1;
	  }
	  c->packet.len = 0;
	  c->state = DATA;
	}
	else if (isdigit(ch))
	  c->length = c->length * 10 + ch - '0';
	break;
      case COMMA:
	if (ch == ',') {
	  c->state = LENGTH;
	  handler(c);
	  c->length = 0;
	}
	break;
      case DATA: ;
      }
    }
  }
  return rd;
}
