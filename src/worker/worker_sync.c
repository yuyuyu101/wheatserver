#include "worker.h"

static struct array *MaxSelectFd = NULL;
static int MaxFd = 0;

int syncSendData(struct client *c, struct slice *data)
{
    if (!isClientValid(c)) {
        return -1;
    }
    if (!data->len)
        return 0;

    insertSliceToSendQueue(c, data);
    ssize_t sended = 0;
    while (isClientNeedSend(c)) {
        sended += clientSendPacketList(c);
        refreshClient(c, Server.cron_time);
        if (!isClientValid(c)) {
            // This function is IO interface, we shouldn't clean client in order
            // to caller to deal with error.
            return -1;
        }
    }

    return sended;
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
    if (c->clifd > MaxFd)
        MaxFd = c->clifd;
    arrayPush(MaxSelectFd, &c->clifd);
    c->is_req = 0;
    return WHEAT_OK;
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
    struct slice slice;
    do {
        n = syncRecvData(client);
        if (!isClientValid(client)) {
            goto cleanup;
        }
        if (msgGetSize(client->req_buf) > stat->stat_buffer_size)
            stat->stat_buffer_size = msgGetSize(client->req_buf);
parser:
        if (!client->pending)
            client->pending = connCreate(client);

        msgRead(client->req_buf, &slice);

        ret = ptcol->parser(client->pending, &slice);
        msgSetReaded(client->req_buf, slice.len);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse http data failed");
            stat->stat_failed_request++;
            goto cleanup;
        }
    } while(ret == 1);
    ret = client->protocol->spotAppAndCall(client->pending);
    if (ret != WHEAT_OK) {
        stat->stat_failed_request++;
        wheatLog(WHEAT_NOTICE, "app failed");
        goto cleanup;
    }
    stat->stat_total_request++;
    client->pending = NULL;
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
        int i;
        fd_set rset;
        tvp.tv_sec = WHEATSERVER_CRON;
        tvp.tv_usec = 0;
        FD_ZERO(&rset);
        for (i = 0; i < narray(MaxSelectFd); i++) {
            int *fd = arrayIndex(MaxSelectFd, i);
            FD_SET(*fd, &rset);
        }

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
    MaxSelectFd = arrayCreate(sizeof(int), 10);
    if (!MaxSelectFd) {
        wheatLog(WHEAT_WARNING, "init failed");
        halt(1);
    }
    arrayPush(MaxSelectFd, &Server.ipfd);
    MaxFd = Server.ipfd;
}
