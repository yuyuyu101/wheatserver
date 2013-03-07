#include "worker.h"

int syncSendData(struct client *c, struct slice *data)
{
    if (!isClientValid(c)) {
        return -1;
    }
    if (!data->len)
        return 0;

    struct slice msg_slice;
    size_t copyed;
    size_t need_write = data->len;
    uint8_t *curr = data->data;
    do {
        msgPut(c->res_buf, &msg_slice);
        copyed = msg_slice.len > need_write ? need_write : msg_slice.len;
        memcpy(msg_slice.data, curr, copyed);
        msgSetWritted(c->res_buf, copyed);
        curr += copyed;
        need_write -= copyed;
    } while(need_write > 0);

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
    } while (n == slice.len);
    if (msgGetSize(c->req_buf) > Server.max_buffer_size)
        setClientUnvalid(c);
    refreshClient(c, Server.cron_time);

    return (int)total;
}

void dispatchRequest(int fd, char *ip, int port)
{
    struct protocol *ptcol = spotProtocol(ip, port, fd);
    struct client *c = createClient(fd, ip, port, ptcol);
    if (c == NULL)
        return ;
    struct workerStat *stat = WorkerProcess->stat;
    int ret, n;
    do {
        n = syncRecvData(c);
        if (n == WHEAT_WRONG) {
            goto cleanup;
        }
        if (msgGetSize(c->req_buf) > stat->stat_buffer_size)
            stat->stat_buffer_size = msgGetSize(c->req_buf);
parser:
        ret = ptcol->parser(c);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse http data failed");
            stat->stat_failed_request++;
            goto cleanup;
        }
    } while(ret == 1);
    ret = c->protocol->spotAppAndCall(c);
    if (ret != WHEAT_OK) {
        stat->stat_failed_request++;
        wheatLog(WHEAT_NOTICE, "app failed");
        goto cleanup;
    }
    stat->stat_total_request++;
    if (msgCanRead(c->req_buf)) {
        resetClientCtx(c);
        goto parser;
    }

cleanup:
    freeClient(c);
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
        FD_SET(Server.ipfd, &rset);

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
}
