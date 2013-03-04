#include "worker.h"

#define WHEAT_ASYNC_CLIENT_MAX     10000

static struct evcenter *WorkerCenter = NULL;
static struct list *Clients = NULL;

static void cleanRequest(struct client *c)
{
    close(c->clifd);
    deleteEvent(WorkerCenter, c->clifd, EVENT_READABLE);
    deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
    struct listNode *node = searchListKey(Clients, c);
    ASSERT(node);
    removeListNode(Clients, node);
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
        deleteEvent(WorkerCenter, c->clifd, EVENT_WRITABLE);
        tryCleanRequest(c);
    }
}

static struct client *initRequest(int fd, char *ip, int port, struct protocol *p)
{
    struct client *c = createClient(fd, ip, port, p);
    c->last_io = Server.cron_time;
    appendToListTail(Clients, c);
    return c;
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
        long idletime = Server.cron_time - c->last_io;
        if (idletime > Server.worker_timeout) {
            wheatLog(WHEAT_VERBOSE,"Closing idle client %d %d", Server.cron_time, c->last_io);
            WorkerProcess->stat->stat_timeout_request++;
            cleanRequest(c);
            continue;
        }

        if ((wstrlen(c->buf) > WHEAT_IOBUF_LEN && idletime > 2) ||
                (wstrlen(c->buf) > Server.max_buffer_size / 2)) {
            if (wstrfree(c->buf) > 1024) {
                wstrRemoveFreeSpace(c->buf);
                continue;
            }
        }
    }
}

int asyncSendData(struct client *c, wstr data)
{
    if (!isClientValid(c))
        return -1;
    if (!wstrlen(data))
        return 0;

    appendToListTail(c->res_buf, data);
    ssize_t sended = clientSendPacketList(c);
    refreshClient(c, Server.cron_time);
    if (!isClientValid(c)) {
        // This function is IO interface, we shouldn't clean client in order
        // to caller to deal with error.
        return -1;
    }
    if (isClientNeedSend(c)) {
        wheatLog(WHEAT_DEBUG, "create write event on asyncSendData %d ", sended);
        createEvent(WorkerCenter, c->clifd, EVENT_WRITABLE, sendReplyToClient, c);
    }

    return sended;
}

int asyncRecvData(struct client *c)
{
    if (!isClientValid(c))
        return -1;
    ssize_t n = readBulkFrom(c->clifd, &c->buf, 0);
    if (n >= 0)
        refreshClient(c, Server.cron_time);
    else
        setClientUnvalid(c);
    return n;
}

void asyncWorkerCron()
{
    int refresh_seconds = Server.stat_refresh_seconds;
    time_t elapse, now = Server.cron_time;
    while (WorkerProcess->alive) {
        processEvents(WorkerCenter, WHEATSERVER_CRON);
        if (WorkerProcess->ppid != getppid()) {
            wheatLog(WHEAT_NOTICE, "parent change, worker shutdown");
            break;
        }
        clientsCron();
        elapse = Server.cron_time;
        if (elapse - now > refresh_seconds) {
            sendStatPacket();
            now = elapse;
        }
        Server.cron_time = time(NULL);
    }
    now = Server.cron_time;
    while (elapse - now > Server.graceful_timeout) {
        elapse = time(NULL);
        processEvents(WorkerCenter, WHEATSERVER_CRON);
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
        } else if (ret == 0) {
            stat->stat_total_request++;
            ret = c->protocol->spotAppAndCall(c);
            resetProtocol(c);
            if (ret != WHEAT_OK) {
                stat->stat_failed_request++;
                c->should_close = 1;
                wheatLog(WHEAT_NOTICE, "app failed");
                break;
            }
        } else if (ret == 1) {
            return ;
        }
    }
    tryCleanRequest(c);
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
    struct client *c = initRequest(cfd, ip, cport, ptcol);
    createEvent(center, cfd, EVENT_READABLE, handleRequest, c);
    WorkerProcess->stat->stat_total_connection++;
}

void setupAsync()
{
    WorkerCenter = eventcenter_init(WHEAT_ASYNC_CLIENT_MAX);
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

