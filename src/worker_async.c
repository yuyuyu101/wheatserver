#include "wheatserver.h"

#define WHEAT_ASYNC_CLIENT_MAX     10000

static struct evcenter *WorkerCenter = NULL;

static void cleanRequest(struct client *c)
{
    close(c->clifd);
    deleteEvent(WorkerCenter, c->clifd, EVENT_READABLE);
    deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
    freeClient(c);
}

void asyncWorkerCron()
{
    int refresh_seconds = Server.stat_refresh_seconds;
    time_t elapse, now = Server.cron_time;
    while (WorkerProcess->alive) {
        processEvents(WorkerCenter, WHEATSERVER_IDLE_TIME);
        if (WorkerProcess->ppid != getppid()) {
            wheatLog(WHEAT_NOTICE, "parent change, worker shutdown");
            return ;
        }
        elapse = Server.cron_time;
        if (elapse - now > refresh_seconds) {
            sendStatPacket();
            now = elapse;
        }
        Server.cron_time = time(NULL);
    }
}

static void handleRequest(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *c = data;
    ssize_t nread, ret = 0;
    struct workerStat *stat = WorkerProcess->stat;
    struct timeval start, end;
    long time_use;
    gettimeofday(&start, NULL);
    nread = asyncRecvData(c);
    if (nread == -1) {
        cleanRequest(c);
        return ;
    }
    if (wstrlen(c->buf) > stat->stat_buffer_size)
        stat->stat_buffer_size = wstrlen(c->buf);
    while (ret == 0 && wstrlen(c->buf)) {
        ret = c->protocol->parser(c);
        if (ret == -1) {
            wheatLog(WHEAT_NOTICE, "parse http data failed:%s", c->buf);
            break;
        }
        if (ret == 0) {
            stat->stat_total_request++;
            ret = c->app->constructor(c);
            if (ret != WHEAT_OK) {
                stat->stat_failed_request++;
                wheatLog(WHEAT_NOTICE, "app construct faileds");
                break;
            }
        }
    }
    cleanRequest(c);
    gettimeofday(&end, NULL);
    time_use = 1000000 * (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec);
    stat->stat_work_time += time_use;
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
    struct app *application = spotAppInterface();
    struct client *c = initClient(cfd, ip, cport, ptcol, application);
    createEvent(center, cfd, EVENT_READABLE, handleRequest, c);
    WorkerProcess->stat->stat_total_connection++;
}

void setupAsync()
{
    WorkerCenter = eventcenter_init(WHEAT_ASYNC_CLIENT_MAX);
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

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *c = data;
    size_t bufpos = 0, totallen = wstrlen(c->res_buf);
    ssize_t nwritten;

    while (bufpos < totallen) {
        nwritten = writeBulkTo(c->clifd, &c->res_buf);
        if (nwritten <= 0)
            break;
        bufpos += nwritten;
    }
    if (nwritten == -1) {
        cleanRequest(c);
        return ;
    }

    if (bufpos >= totallen) {
        deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
    }
}

int asyncSendData(struct client *c)
{
    size_t bufpos = 0, totallen = wstrlen(c->res_buf);
    ssize_t nwritten;

    while (bufpos < totallen) {
        nwritten = writeBulkTo(c->clifd, &c->res_buf);
        if (nwritten <= 0)
            break;
        bufpos += nwritten;
    }
    if (nwritten == -1) {
        cleanRequest(c);
        return -1;
    }

    if (bufpos < totallen) {
        createEvent(WorkerCenter, c->clifd, EVENT_WRITABLE, sendReplyToClient, c);
    }
    return nwritten;
}

int asyncRecvData(struct client *c)
{
    return readBulkFrom(c->clifd, &c->buf);
}
