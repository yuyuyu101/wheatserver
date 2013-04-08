// Asynchronous worker module implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "../wheatserver.h"

int asyncSendData(struct conn *c); // pass `data` ownership to
int asyncRecvData(struct client *c);

static struct moduleAttr AsyncWorkerAttr = {
    "AsyncWorker", NULL, 0, NULL, 0
};

struct worker AsyncWorker = {
    &AsyncWorkerAttr, NULL, NULL, asyncSendData,
    asyncRecvData
};

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *c = data;
    if (!isClientValid(c))
        return ;

    refreshClient(c);

    clientSendPacketList(c);
    if (!isClientValid(c) || !isClientNeedSend(c)) {
        wheatLog(WHEAT_DEBUG, "delete write event on sendReplyToClient");
        deleteEvent(WorkerProcess->center, c->clifd, EVENT_WRITABLE);
        tryFreeClient(c);
    }
}

int asyncSendData(struct conn *c)
{
    if (!isClientValid(c->client))
        return WHEAT_WRONG;

    struct client *client = c->client;
    clientSendPacketList(client);
    refreshClient(client);
    if (!isClientValid(client)) {
        // This function is IO interface, we shouldn't clean client in order
        // to caller to deal with error.
        return WHEAT_WRONG;
    }
    if (isClientNeedSend(client)) {
        wheatLog(WHEAT_DEBUG, "create write event on asyncSendData");
        createEvent(WorkerProcess->center, client->clifd, EVENT_WRITABLE,
                sendReplyToClient, client);
    }

    return WHEAT_OK;
}

int asyncRecvData(struct client *c)
{
    if (!isClientValid(c))
        return -1;
    ssize_t n;
    size_t total = 0;
    struct slice slice;
    // Because os IO notify only once if you don't read all data within this
    // buffer
    do {
        n = msgPut(c->req_buf, &slice);
        if (n != 0) {
            break;
        }
        n = readBulkFrom(c->clifd, &slice);
        if (n < 0) {
            setClientUnvalid(c);
            break;
        }
        total += n;
        msgSetWritted(c->req_buf, n);
    } while (n == slice.len);
    if (msgGetSize(c->req_buf) > Server.max_buffer_size) {
        wheatLog(WHEAT_VERBOSE, "Client buffer size larger than limit %d>%d",
                msgGetSize(c->req_buf), Server.max_buffer_size);
        setClientUnvalid(c);
    }
    refreshClient(c);
    return (int)total;
}
