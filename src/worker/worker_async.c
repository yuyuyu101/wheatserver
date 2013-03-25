// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "worker.h"

#define WHEAT_ASYNC_CLIENT_MAX     1000

static struct evcenter *WorkerCenter = NULL;
static struct list *Clients = NULL;

// Cache below stat field avoid too much query on StatItems
// --------- Statistic Cache --------------
static long long StatTotalClient = 0;
static long long StatBufferSize = 0;
static long long StatTotalRequest = 0;
static long long StatFailedRequest = 0;
static long long StatRunTime = 0;
//-----------------------------------------


static void handleRequest(struct evcenter *center, int fd, void *data, int mask);

static void cleanRequest(struct client *c)
{
    deleteEvent(WorkerCenter, c->clifd, EVENT_READABLE);
    deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
    freeClient(c);
}

static void tryCleanRequest(struct client *c)
{
    if (isClientValid(c) && (isClientNeedSend(c) || !c->should_close))
        return;
    cleanRequest(c);
}

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *c = data;
    if (!isClientValid(c))
        return ;

    refreshClient(c, Server.cron_time);

    clientSendPacketList(c);
    if (!isClientValid(c) || !isClientNeedSend(c)) {
        wheatLog(WHEAT_DEBUG, "delete write event on sendReplyToClient");
        deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
        tryCleanRequest(c);
    }
}

static void clientsCron()
{
    unsigned long numclients = listLength(Clients);
    unsigned long iteration = numclients < 50 ? numclients : numclients / 10;
    struct client *c = NULL;
    struct listNode *node = NULL;

    while (listLength(Clients) && iteration--) {
        node = listFirst(Clients);
        c = listNodeValue(node);
        ASSERT(c);

        listRotate(Clients);
        long idletime = Server.cron_time.tv_sec - c->last_io.tv_sec;
        if (idletime > Server.worker_timeout) {
            wheatLog(WHEAT_VERBOSE,"Closing idle client %d %d", Server.cron_time, c->last_io);
            getStatItemByName("Total timeout client")->val++;
            cleanRequest(c);
            continue;
        }
    }
}

int asyncSendData(struct conn *c)
{
    if (!isClientValid(c->client))
        return WHEAT_WRONG;

    struct client *client = c->client;
    clientSendPacketList(client);
    refreshClient(client, Server.cron_time);
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
    refreshClient(c, Server.cron_time);
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
    WorkerProcess->refresh_time = Server.cron_time.tv_sec;
    while (WorkerProcess->alive) {
        processEvents(WorkerCenter, WHEATSERVER_CRON);
        if (WorkerProcess->ppid != getppid()) {
            wheatLog(WHEAT_NOTICE, "parent change, worker shutdown");
            break;
        }
        clientsCron();
        appCron();
        elapse = Server.cron_time.tv_sec;
        if (elapse - WorkerProcess->refresh_time > refresh_seconds) {
            getStatValByName("Total client") = StatTotalClient;
            getStatValByName("Max buffer size")= StatBufferSize;
            getStatValByName("Total request")= StatTotalRequest;
            getStatValByName("Total failed request")= StatFailedRequest;
            getStatValByName("Worker run time")= StatRunTime;
            sendStatPacket(WorkerProcess);
            StatTotalClient = 0;
            StatBufferSize = 0;
            StatTotalRequest = 0;
            StatFailedRequest = 0;
            StatRunTime = 0;
            WorkerProcess->refresh_time = elapse;
        }
        gettimeofday(&Server.cron_time, NULL);
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
        cleanRequest(client);
        return ;
    }
    if (msgGetSize(client->req_buf) > StatBufferSize) {
        StatBufferSize = msgGetSize(client->req_buf);
    }
    while (msgCanRead(client->req_buf)) {
        conn = connGet(client);

        msgRead(client->req_buf, &slice);
        ret = client->protocol->parser(conn, &slice, &parsed);
        msgSetReaded(client->req_buf, parsed);

        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse data failed");
            break;
        } else if (ret == WHEAT_OK) {
            StatTotalRequest++;
            ret = client->protocol->spotAppAndCall(conn);
            client->pending = NULL;
            if (ret != WHEAT_OK) {
                StatFailedRequest++;
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
    StatRunTime += time_use;
}

static void acceptClient(struct evcenter *center, int fd, void *data, int mask)
{
    char ip[46];
    int cport, cfd = wheatTcpAccept(Server.neterr, fd, ip, &cport);
    if (cfd == NET_WRONG) {
        if (errno != EAGAIN)
            wheatLog(WHEAT_WARNING, "Accepting client connection failed: %s", Server.neterr);
        return;
    }
    wheatNonBlock(Server.neterr, cfd);
    wheatCloseOnExec(Server.neterr, cfd);

    struct protocol *ptcol = spotProtocol(ip, cport, cfd);
    if (!ptcol) {
        close(cfd);
        wheatLog(WHEAT_WARNING, "spot protocol failed");
        return ;
    }
    struct client *c = createClient(cfd, ip, cport, ptcol);
    createEvent(center, cfd, EVENT_READABLE, handleRequest, c);
    StatTotalClient++;
}

void setupAsync()
{
    WorkerCenter = eventcenterInit(WHEAT_ASYNC_CLIENT_MAX);
    Clients = createList();
    if (!WorkerCenter) {
        wheatLog(WHEAT_WARNING, "eventcenter_init failed");
        halt(1);
    }

   if (createEvent(WorkerCenter, Server.ipfd, EVENT_READABLE, acceptClient,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        halt(1);
    }
}
