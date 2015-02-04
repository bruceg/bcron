#include <bglibs/sysdeps.h>
#include <errno.h>
#include <unistd.h>
#include "bcron.h"

static str packet;

int sendpacket(int fd, const str* s)
{
  const char* ptr;
  long wr;
  long len;
  packet.len = 0;
  if (!str_catu(&packet, s->len)
      || !str_catc(&packet, ':')
      || !str_cat(&packet, s)
      || !str_catc(&packet, ',')) {
    errno = ENOMEM;
    return -1;
  }
  len = packet.len;
  ptr = packet.s;
  while (len > 0) {
    if ((wr = write(fd, ptr, len)) <= 0)
      return wr;
    len -= wr;
    ptr += wr;
  }
  return packet.len;
}
