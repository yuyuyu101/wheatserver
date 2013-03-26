// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "worker.h"

static struct list *MaxSelectFd = NULL;
static long MaxFd = 0;

// Cache below stat field avoid too much query on StatItems
// --------- Statistic Cache --------------
static long long StatTotalClient = 0;
static long long StatBufferSize = 0;
static long long StatTotalRequest = 0;
static long long StatFailedRequest = 0;
static long long StatRunTime = 0;
//-----------------------------------------

int syncSendData(struct conn *c)
{
    if (!isClientValid(c->client)) {
        return -1;
    }
    struct client *client = c->client;
    while (isClientNeedSend(client)) {
        clientSendPacketList(client);
        refreshClient(client, Server.cron_time);
        if (!isClientValid(client)) {
            // This function is IO interface, we shouldn't clean client in order
            // to caller to deal with error.
            return WHEAT_WRONG;
        }
    }

    return WHEAT_OK;
}

static int syncRecvData(struct client *c)
{
    if (!isClientValid(c)) {
        return -1;
    }
    struct slice slice;
    ssize_t n, total = 0;
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
    } while (n == slice.len || n == 0);
    if (msgGetSize(c->req_buf) > Server.max_buffer_size) {
        wheatLog(WHEAT_VERBOSE, "Client buffer size larger than limit %d>%d",
                msgGetSize(c->req_buf), Server.max_buffer_size);
        setClientUnvalid(c);
    }
    refreshClient(c, Server.cron_time);

    return (int)total;
}

int syncRegisterRead(struct client *c)
{
    intptr_t fd = c->clifd;
    if (fd > MaxFd)
        MaxFd = fd;
    appendToListTail(MaxSelectFd, (void*)fd);
    return WHEAT_OK;
}

void syncUnregisterRead(struct client *c)
{
    intptr_t fd = c->clifd;
    struct listNode *node = searchListKey(MaxSelectFd, (void*)fd);
    removeListNode(MaxSelectFd, node);
}

void dispatchRequest(int fd, char *ip, int port)
{
    struct protocol *ptcol = spotProtocol(ip, port, fd);
    if (!ptcol) {
        wheatLog(WHEAT_WARNING, "spot protocol failed");
        close(fd);
        return ;
    }
    struct client *client = createClient(fd, ip, port, ptcol);
    if (client == NULL)
        return ;
    int ret, n;
    size_t nparsed;
    struct slice slice;
    struct conn *conn = NULL;
    do {
        n = syncRecvData(client);
        if (!isClientValid(client)) {
            goto cleanup;
        }
        if (msgGetSize(client->req_buf) > StatBufferSize) {
            getStatItemByName("Max buffer size")->val = msgGetSize(client->req_buf);
            StatBufferSize = msgGetSize(client->req_buf);
        }
parser:
        conn = connGet(client);

        msgRead(client->req_buf, &slice);

        ret = ptcol->parser(conn, &slice, &nparsed);
        msgSetReaded(client->req_buf, nparsed);
        if (ret == WHEAT_WRONG) {
            setClientUnvalid(client);
            wheatLog(WHEAT_NOTICE, "parse data failed");
            StatFailedRequest++;
            goto cleanup;
        } else if (ret == 1) {
            client->pending = conn;
        }
    } while(ret == 1);
    client->pending = NULL;
    ret = client->protocol->spotAppAndCall(conn);
    if (ret != WHEAT_OK) {
        StatFailedRequest++;
        wheatLog(WHEAT_NOTICE, "app failed");
        goto cleanup;
    }
    StatTotalRequest++;
    if (msgCanRead(client->req_buf)) {
        goto parser;
    }

cleanup:
    freeClient(client);
}

void syncWorkerCron()
{
    struct timeval start, end;
    int refresh_seconds = Server.stat_refresh_seconds;
    long time_use;
    time_t elapse = Server.cron_time.tv_sec;
    while (WorkerProcess->alive) {
        int fd, ret;

        char ip[46];
        int port;
        elapse = time(NULL);
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
        fd = wheatTcpAccept(Server.neterr, Server.ipfd, ip, &port);
        if (fd == NET_WRONG) {
            goto accepterror;
        }
        if ((ret = wheatNonBlock(Server.neterr, fd)) == NET_WRONG)
            goto accepterror;
        if ((ret = wheatCloseOnExec(Server.neterr, fd)) == NET_WRONG)
            goto accepterror;

        gettimeofday(&start, NULL);
        dispatchRequest(fd, ip, port);
        gettimeofday(&end, NULL);
        time_use = 1000000 * (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec);
        StatRunTime += time_use;
        StatTotalClient++;
        Server.cron_time = end;

        continue;
accepterror:
        if (errno != EAGAIN)
            wheatLog(WHEAT_NOTICE, "workerCron: %s", Server.neterr);
        struct timeval tvp;
        fd_set rset;
        tvp.tv_sec = WHEATSERVER_CRON;
        tvp.tv_usec = 0;
        FD_ZERO(&rset);
        struct listNode *node = NULL;
        struct listIterator *iter = listGetIterator(MaxSelectFd, START_HEAD);
        while ((node = listNext(iter)) != NULL) {
            long fd = (intptr_t)listNodeValue(node);
            FD_SET(fd, &rset);
        }
        freeListIterator(iter);
        workerProcessCron();

        ret = select(Server.ipfd+1, &rset, NULL, NULL, &tvp);
        if (ret >= 0)
            continue;
        else {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            wheatLog(WHEAT_WARNING, "workerCron() select failed: %s", strerror(errno));
            return ;
        }
    }
}

void setupSync()
{
    MaxSelectFd = createList();
    if (!MaxSelectFd) {
        wheatLog(WHEAT_WARNING, "init failed");
        halt(1);
    }
    intptr_t fd = Server.ipfd;
    appendToListTail(MaxSelectFd, (void*)fd);
    MaxFd = Server.ipfd;
}
