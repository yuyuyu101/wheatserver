// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "../wheatserver.h"

int syncSendData(struct conn *c); // pass `data` ownership to
int syncRecvData(struct client *c);

static struct moduleAttr SyncWorkerAttr = {
    "SyncWorker", NULL, 0, NULL, 0
};

struct worker SyncWorker = {
    &SyncWorkerAttr, NULL, NULL, syncSendData,
    syncRecvData
};

int syncSendData(struct conn *c)
{
    if (!isClientValid(c->client)) {
        return -1;
    }
    struct client *client = c->client;
    while (isClientNeedSend(client)) {
        clientSendPacketList(client);
        refreshClient(client);
        if (!isClientValid(client)) {
            // This function is IO interface, we shouldn't clean client in order
            // to caller to deal with error.
            return WHEAT_WRONG;
        }
    }

    return WHEAT_OK;
}

int syncRecvData(struct client *c)
{
    if (!isClientValid(c)) {
        return -1;
    }
    struct slice slice;
    ssize_t n, total = 0;
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
    } while (n == slice.len || n == 0);
    if (msgGetSize(c->req_buf) > Server.max_buffer_size) {
        wheatLog(WHEAT_VERBOSE, "Client buffer size larger than limit %d>%d",
                msgGetSize(c->req_buf), Server.max_buffer_size);
        setClientUnvalid(c);
    }
    refreshClient(c);

    return (int)total;
}
