#include "worker.h"

static struct list *MaxSelectFd = NULL;
static long MaxFd = 0;

int syncSendData(struct conn *c, struct slice *data)
{
    if (!isClientValid(c->client)) {
        return -1;
    }
    if (!data->len)
        return 0;

    appendSliceToSendQueue(c, data);
    ssize_t sended = 0;
    struct client *client = c->client;
    while (isClientNeedSend(client)) {
        sended += clientSendPacketList(client);
        refreshClient(client, Server.cron_time);
        if (!isClientValid(client)) {
            // This function is IO interface, we shouldn't clean client in order
            // to caller to deal with error.
            return -1;
        }
    }

    return sended;
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
    if (msgGetSize(c->req_buf) > Server.max_buffer_size)
        setClientUnvalid(c);
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
    struct workerStat *stat = WorkerProcess->stat;
    int ret, n;
    size_t nparsed;
    struct slice slice;
    struct conn *conn = NULL;
    do {
        n = syncRecvData(client);
        if (!isClientValid(client)) {
            goto cleanup;
        }
        if (msgGetSize(client->req_buf) > stat->stat_buffer_size)
            stat->stat_buffer_size = msgGetSize(client->req_buf);
parser:
        conn = connGet(client);

        msgRead(client->req_buf, &slice);

        ret = ptcol->parser(conn, &slice, &nparsed);
        msgSetReaded(client->req_buf, nparsed);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse http data failed");
            stat->stat_failed_request++;
            goto cleanup;
        } else if (ret == 1) {
            client->pending = conn;
        }
    } while(ret == 1);
    ret = client->protocol->spotAppAndCall(conn);
    if (ret != WHEAT_OK) {
        stat->stat_failed_request++;
        wheatLog(WHEAT_NOTICE, "app failed");
        goto cleanup;
    }
    stat->stat_total_request++;
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
    time_t elapse, now = time(NULL);
    struct workerStat *stat = WorkerProcess->stat;
    while (WorkerProcess->alive) {
        int fd, ret;

        char ip[46];
        int port;
        elapse = time(NULL);
        if (elapse - now > refresh_seconds) {
            sendStatPacket();
            now = elapse;
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
        stat->stat_work_time += time_use;
        stat->stat_total_connection++;

        continue;
accepterror:
        if (errno != EAGAIN)
            wheatLog(WHEAT_NOTICE, "workerCron: %s", Server.neterr);
        if (WorkerProcess->ppid != getppid()) {
            wheatLog(WHEAT_NOTICE, "parent change, worker shutdown");
            return ;
        }
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
        appCron();

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
