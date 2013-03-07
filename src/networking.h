#ifndef WHEATSERVER_NETWORKING_H
#define WHEATSERVER_NETWORKING_H

#include "wheatserver.h"

#define WHEAT_IOBUF_LEN 1024 * 4

struct masterClient;
int readBulkFrom(int fd, struct slice *slice);
int writeBulkTo(int fd, struct slice *clientbuf);
void replyMasterClient(struct masterClient *c, const char *buf, size_t len);
ssize_t isClientPreparedWrite(int fd, struct evcenter *center, void *c);

#endif
