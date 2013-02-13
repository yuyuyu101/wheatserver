#ifndef _NETWORKING_H
#define _NETWORKING_H

#include "wstr.h"

#define WHEAT_IOBUF_LEN 1024 * 4

int readBulkFrom(int fd, wstr *buf);
int writeBulkTo(int fd, wstr *buf);
void addReply(struct masterClient *c, const char *buf, size_t len);

#endif
