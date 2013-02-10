#include "wheatserver.h"

void dispatchRequest(int fd, char *ip, int port)
{
    struct protocol *ptcol = spotProtocol(ip, port, fd);
    struct app *application = spotAppInterface();
    struct client *c = initClient(fd, ip, port, ptcol, application);
    if (c == NULL)
        return ;
    struct workerStat *stat = WorkerProcess->stat;
    int ret, n;
    do {
        n = syncRecvData(fd, &c->buf);
        if (n == WHEAT_WRONG) {
            goto cleanup;
        }
        if (wstrlen(c->buf) > stat->stat_buffer_size)
            stat->stat_buffer_size = wstrlen(c->buf);
parser:
        ret = ptcol->parser(c);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse http data failed:%s", c->buf);
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
    if (ret == WHEAT_WRONG)
        goto cleanup;
    stat->stat_total_request++;
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

int syncSendData(int fd, wstr *clientbuf)
{
    wstr buf = *clientbuf;
    ssize_t bufpos = 0, nwritten = 0, totalwritten = 0;
    while(bufpos <= wstrlen(buf)) {
        nwritten = writeBulkTo(fd, clientbuf);
        if (nwritten == -1)
            break;
        bufpos += nwritten;
        totalwritten += nwritten;
        wstrRange(buf, bufpos, 0);
    }
    return (int)totalwritten;
}

int syncRecvData(int fd, wstr *buf)
{
    return readBulkFrom(fd, buf);
}
