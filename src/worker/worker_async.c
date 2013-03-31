// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "worker.h"

#define WHEAT_ASYNC_CLIENT_MAX     1000

static struct evcenter *WorkerCenter = NULL;

// Cache below stat field avoid too much query on StatItems
// --------- Statistic Cache --------------
static struct statItem *StatBufferSize = NULL;
static struct statItem *StatTotalRequest = NULL;
static struct statItem *StatFailedRequest = NULL;
static struct statItem *StatRunTime = NULL;
//-----------------------------------------

void setupAsync();
void asyncWorkerCron();
int asyncSendData(struct conn *c); // pass `data` ownership to
int asyncRegisterRead(struct client *c);
void asyncUnregisterRead(struct client *c);

static struct moduleAttr AsyncWorkerAttr = {
    "AsyncWorker", NULL, 0, NULL, 0
};

struct worker AsyncWorker = {
    &AsyncWorkerAttr, setupAsync, asyncWorkerCron, asyncSendData,
    asyncRegisterRead, asyncUnregisterRead,
};


static void handleRequest(struct evcenter *center, int fd, void *data, int mask);

static void deleteEvents(struct client *c)
{
    deleteEvent(WorkerCenter, c->clifd, EVENT_READABLE);
    deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
}

static void tryCleanRequest(struct client *c)
{
    if (isClientValid(c) && (isClientNeedSend(c) || !c->should_close))
        return;
    freeClient(c);
}

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *c = data;
    if (!isClientValid(c))
        return ;

    refreshClient(c);

    clientSendPacketList(c);
    if (!isClientValid(c) || !isClientNeedSend(c)) {
        wheatLog(WHEAT_DEBUG, "delete write event on sendReplyToClient");
        deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
        tryCleanRequest(c);
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
        createEvent(WorkerCenter, client->clifd, EVENT_WRITABLE, sendReplyToClient, client);
    }

    return WHEAT_OK;
}

static int asyncRecvData(struct client *c)
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
            c->err = "msg put failed";
            break;
        }
        n = readBulkFrom(c->clifd, &slice);
        if (n < 0) {
            c->err = "async RecvData failed";
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

int asyncRegisterRead(struct client *c)
{
    int ret = createEvent(WorkerCenter, c->clifd, EVENT_READABLE, handleRequest, c);
    return ret;
}

void asyncUnregisterRead(struct client *c)
{
    deleteEvent(WorkerCenter, c->clifd, EVENT_READABLE);
}

void asyncWorkerCron()
{
    int refresh_seconds = Server.stat_refresh_seconds;
    time_t elapse = Server.cron_time.tv_sec;
    while (WorkerProcess->alive) {
        processEvents(WorkerCenter, WHEATSERVER_CRON);
        workerProcessCron();
        elapse = Server.cron_time.tv_sec;
        if (elapse - WorkerProcess->refresh_time > refresh_seconds) {
            sendStatPacket(WorkerProcess);
            WorkerProcess->refresh_time = elapse;
        }
    }
    WorkerProcess->refresh_time = Server.cron_time.tv_sec;
    while (elapse - WorkerProcess->refresh_time > Server.graceful_timeout) {
        elapse = time(NULL);
        processEvents(WorkerCenter, WHEATSERVER_CRON);
    }
}

static void handleRequest(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *client = data;
    struct conn *conn = NULL;
    ssize_t nread, ret = 0;
    struct timeval start, end;
    long time_use;
    struct slice slice;
    size_t parsed = 0;
    gettimeofday(&start, NULL);
    nread = asyncRecvData(client);
    if (!isClientValid(client)) {
        freeClient(client);
        return ;
    }
    if (msgGetSize(client->req_buf) > getStatVal(StatBufferSize)) {
        getStatVal(StatBufferSize) = msgGetSize(client->req_buf);
    }
    while (msgCanRead(client->req_buf)) {
        conn = connGet(client);

        msgRead(client->req_buf, &slice);
        ret = client->protocol->parser(conn, &slice, &parsed);
        msgSetReaded(client->req_buf, parsed);

        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse data failed");
            setClientUnvalid(client);
            break;
        } else if (ret == WHEAT_OK) {
            getStatVal(StatTotalRequest)++;
            client->pending = NULL;
            ret = client->protocol->spotAppAndCall(conn);
            if (ret != WHEAT_OK) {
                getStatVal(StatFailedRequest)++;
                client->should_close = 1;
                wheatLog(WHEAT_NOTICE, "app failed");
                break;
            }
        } else if (ret == 1) {
            client->pending = conn;
            continue;
        }
    }
    tryCleanRequest(client);
    gettimeofday(&end, NULL);
    time_use = 1000000 * (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec);
    getStatVal(StatRunTime) += time_use;
}

static void acceptClient(struct evcenter *center, int fd, void *data, int mask)
{
    char ip[46];
    struct client *c;
    int cport, cfd;

    cfd = wheatTcpAccept(Server.neterr, fd, ip, &cport);
    if (cfd == NET_WRONG) {
        if (errno != EAGAIN)
            wheatLog(WHEAT_WARNING, "Accepting client connection failed: %s", Server.neterr);
        return;
    }
    wheatNonBlock(Server.neterr, cfd);
    wheatCloseOnExec(Server.neterr, cfd);

    c = createClient(cfd, ip, cport, WorkerProcess->protocol);
    createEvent(center, cfd, EVENT_READABLE, handleRequest, c);
    setClientFreeNotify(c, (void (*)(void*))deleteEvents);
}

void setupAsync()
{
    WorkerCenter = eventcenterInit(WHEAT_ASYNC_CLIENT_MAX);
    if (!WorkerCenter) {
        wheatLog(WHEAT_WARNING, "eventcenter_init failed");
        halt(1);
    }

   if (createEvent(WorkerCenter, Server.ipfd, EVENT_READABLE, acceptClient,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        halt(1);
    }
    StatBufferSize = getStatItemByName("Max buffer size");
    StatTotalRequest = getStatItemByName("Total request");
    StatFailedRequest = getStatItemByName("Total failed request");
    StatRunTime = getStatItemByName("Worker run time");
}
