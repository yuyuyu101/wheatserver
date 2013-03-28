// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "networking.h"


/* return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN */
int readBulkFrom(int fd, struct slice *slice)
{
    ssize_t nread;

    nread = read(fd, slice->data, slice->len);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            wheatLog(WHEAT_NOTICE, "Reading from fd %d: %s", fd, strerror(errno));
            return WHEAT_WRONG;
        }
    } else if (nread == 0) {
        wheatLog(WHEAT_DEBUG, "Peer close file descriptor %d", fd);
        return WHEAT_WRONG;
    }
    return (int)nread;
}

int syncReadBulkFrom(int fd, struct slice *slice)
{
   int read = 0;

   do {
       read = readBulkFrom(fd, slice);
   } while(read == 0);
   return read;
}

/* return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN */
int writeBulkTo(int fd, struct slice *slice)
{
    ssize_t nwritten = 0;
    nwritten = write(fd, slice->data, slice->len);
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else if (errno == EPIPE) {
            wheatLog(WHEAT_DEBUG, "Receive RST, peer closed", strerror(errno));
            return WHEAT_WRONG;
        } else {
            wheatLog(WHEAT_NOTICE,
                "Error writing to client: %s", strerror(errno));
            return WHEAT_WRONG;
        }
    }
    return (int)nwritten;
}

int syncWriteBulkTo(int fd, struct slice *slice)
{
    int totallen, ret;
    totallen = 0;
    while (totallen < slice->len) {
        ret = writeBulkTo(fd, slice);
        if (ret == -1)
            return WHEAT_WRONG;
        totallen += ret;
    }
    return totallen;
}

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct masterClient *client = data;
    size_t bufpos = 0, totallen = wstrlen(client->response_buf);
    ssize_t nwritten = 0;

    while (bufpos < totallen) {
        struct slice slice;
        sliceTo(&slice, (uint8_t *)client->response_buf,
                wstrlen(client->response_buf));
        nwritten = writeBulkTo(client->fd, &slice);
        if (nwritten <= 0)
            break;
        bufpos += nwritten;
    }
    if (nwritten == -1) {
        freeMasterClient(client);
        return ;
    }
    if (bufpos == totallen) {
        deleteEvent(center, fd, EVENT_WRITABLE);
    }
}

ssize_t isClientPreparedWrite(int fd, struct evcenter *center, void *c)
{
    if (fd <= 0 || createEvent(center, fd, EVENT_WRITABLE, sendReplyToClient, c) == WHEAT_WRONG)
        return WHEAT_WRONG;
    return WHEAT_OK;
}

void replyMasterClient(struct masterClient *c, const char *buf, size_t len)
{
    if (isClientPreparedWrite(c->fd, Server.master_center, c) == WHEAT_WRONG)
        return ;
    c->response_buf = wstrCatLen(c->response_buf, buf, len);
}
