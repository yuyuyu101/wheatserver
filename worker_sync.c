#include "wheatserver.h"

void dispatchRequest(int fd, char *ip, int port)
{
    struct protocol *ptcol = spotProtocol(ip, port, fd);
    struct app *applicantion = spotAppInterface();
    struct client *c = initClient(fd, ip, port, ptcol, applicantion);
    if (c == NULL)
        return ;
    int ret;
    do {
        int n = readBulkFrom(fd, &c->buf);
        if (n == -1) {
            freeClient(c);
            return ;
        }
        ret = ptcol->parser(c);
        if (ret == -1) {
            freeClient(c);
            return ;
        }
    }while(ret != 1);
    applicantion->constructor(c);
    close(fd);
    freeClient(c);
}

void syncWorkerCron()
{
    while (WorkerProcess->alive) {
        int fd, ret;

        char ip[46];
        int port;
        fd = wheatTcpAccept(Server.neterr, Server.ipfd, ip, &port);
        if (fd == NET_WRONG)
            goto accepterror;
        if ((ret = wheatNonBlock(Server.neterr, fd)) == NET_WRONG)
            goto accepterror;
        if ((ret = wheatCloseOnExec(Server.neterr, fd)) == NET_WRONG)
            goto accepterror;
        dispatchRequest(fd, ip, port);
        continue;
accepterror:
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

/* SyncWorker entry */
void setupSync()
{
}
