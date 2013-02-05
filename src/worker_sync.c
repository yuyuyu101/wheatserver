#include "wheatserver.h"

void dispatchRequest(int fd, char *ip, int port)
{
    struct protocol *ptcol = spotProtocol(ip, port, fd);
    struct app *application = spotAppInterface();
    struct client *c = initClient(fd, ip, port, ptcol, application);
    struct workerStat *stat = WorkerProcess->stat;
    if (c == NULL)
        return ;
    int ret;
    int loop = 0;
    do {
        loop++;
        int n = syncRecvData(fd, &c->buf);
        if (n == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "receive data failed:%s loop:%d", c->buf, loop);
            goto cleanup;
        }
        if (wstrlen(c->buf) > stat->stat_buffer_size)
            stat->stat_buffer_size = wstrlen(c->buf);
parser:
        stat->stat_total_request++;
        ret = ptcol->parser(c);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse http data failed:%s loop:%d", c->buf, loop);
            stat->stat_failed_request++;
            goto cleanup;
        }
    } while(ret == 1);
    ret = application->constructor(c);
    if (ret != WHEAT_OK) {
        stat->stat_failed_request++;
        wheatLog(WHEAT_NOTICE, "app construct faileds");
    }
    ret = syncSendData(fd, &c->res_buf);
    if (ret != WHEAT_OK)
        goto cleanup;
    loop = 0;
    if (wstrlen(c->buf)) {
        c->protocol->freeProtocolData(c->protocol_data);
        c->app->freeAppData(c->app_private_data);
        c->app_private_data = application->initAppData();
        c->protocol_data = c->protocol->initProtocolData();
        goto parser;
    }

cleanup:
    close(fd);
    freeClient(c);
}

void syncWorkerCron()
{
    if (wheatNonBlock(Server.neterr, Server.ipfd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set nonblock %d failed: %s", Server.ipfd, Server.neterr);
        halt(1);
    }

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
        if (now - elapse > refresh_seconds) {
            sendStatPacket();
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
        tvp.tv_sec = Server.idle_timeout;
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

int syncSendData(int fd, wstr *buf)
{
    return writeBulkTo(fd, buf);
}

int syncRecvData(int fd, wstr *buf)
{
    return readBulkFrom(fd, buf);
}
