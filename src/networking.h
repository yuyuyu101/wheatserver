#ifndef WHEATSERVER_NETWORKING_H
#define WHEATSERVER_NETWORKING_H

#include "wstr.h"

#define WHEAT_IOBUF_LEN 1024 * 4

int readBulkFrom(int fd, wstr *clientbuf, size_t limit);
int writeBulkTo(int fd, wstr *buf);
void replyMasterClient(struct masterClient *c, const char *buf, size_t len);
ssize_t isClientPreparedWrite(int fd, struct evcenter *center, void *c);

#endif
