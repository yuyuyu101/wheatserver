#include "worker.h"

struct workerProcess *WorkerProcess = NULL;

/* ========== Worker Area ========== */

static struct list *ClientPool = NULL;

struct worker *spotWorker(char *worker_name)
{
    int i;
    for (i = 0; i < WHEAT_WORKERS; i++) {
        if (strcmp(WorkerTable[i].name, worker_name) == 0) {
            return &WorkerTable[i];
        }
    }
    return NULL;
}

void workerProcessCron()
{
    ASSERT(WorkerProcess);
    if (wheatNonBlock(Server.neterr, Server.ipfd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set nonblock %d failed: %s", Server.ipfd, Server.neterr);
        halt(1);
    }

    Server.cron_time = time(NULL);
    sendStatPacket();
    WorkerProcess->worker->cron();
}

static struct list *createAndFillPool()
{
    struct list *l = createList();
    int i = 0;
    ASSERT(l);
    for (; i < 120; ++i) {
        struct client *c = wmalloc(sizeof(*c));
        appendToListTail(l, c);
    }
    return l;
}

void initWorkerProcess(struct workerProcess *worker, char *worker_name)
{
    if (Server.stat_fd != 0)
        close(Server.stat_fd);
    setProctitle(worker_name);
    worker->pid = getpid();
    worker->ppid = getppid();
    worker->alive = 1;
    worker->start_time = time(NULL);
    worker->worker_name = worker_name;
    worker->worker = spotWorker(worker_name);
    ASSERT(worker->worker);
    worker->stat = initWorkerStat(0);
    initWorkerSignals();
    ClientPool = createAndFillPool();
    worker->worker->setup();
}

void freeWorkerProcess(void *w)
{
    struct workerProcess *worker = w;
    wfree(worker->stat);
    wfree(worker);
}

int initAppData(struct conn *c)
{
    if (c->app) {
        c->app_private_data = c->app->initAppData(c);
    }
    return WHEAT_OK;
}

void freeAppData(struct conn *c)
{
    if (c->app_private_data)
        c->app->freeAppData(c->app_private_data);
    c->app_private_data = NULL;
}

struct conn *connCreate(struct client *client)
{
    struct conn *c = wmalloc(sizeof(*c));
    c->client = client;
    c->protect = NULL;
    c->protocol_data = client->protocol->initProtocolData();
    c->app = c->app_private_data = NULL;
    c->node = appendToListTail(client->conns, c);
    return c;
}

static void connDealloc(struct conn *c)
{
    if (c->protocol_data)
        c->client->protocol->freeProtocolData(c->protocol_data);
    if (c->app_private_data)
        c->app->freeAppData(c->app_private_data);
    wfree(c);
}

void insertSliceToSendQueue(struct client *client, struct slice *s)
{
    struct sendPacket *packet = wmalloc(sizeof(*packet));
    if (!packet)
        setClientUnvalid(client);
    packet->type = SLICE;
    sliceTo(&packet->target.slice, s->data, s->len);
    appendToListTail(client->send_queue, packet);
}

void freeSendPacket(struct sendPacket *p)
{
    wfree(p);
}

struct client *createClient(int fd, char *ip, int port, struct protocol *p)
{
    struct client *c;
    struct listNode *node;
    if ((node = listFirst(ClientPool)) == NULL) {
        c = wmalloc(sizeof(*c));
    } else {
        c = listNodeValue(node);
        removeListNode(ClientPool, node);
    }
    if (c == NULL)
        return NULL;
    c->clifd = fd;
    c->ip = wstrNew(ip);
    c->port = port;
    c->protocol = p;
    c->conns = createList();
    listSetFree(c->conns, (void(*)(void *))connDealloc);
    c->err = NULL;
    c->req_buf = msgCreate(Server.mbuf_size);
    c->send_queue = createList();
    listSetFree(c->send_queue, (void(*)(void*))freeSendPacket);
    c->is_req = 1;
    c->should_close = 0;
    c->valid = 1;
    c->pending = NULL;
    return c;
}

void freeClient(struct client *c)
{
    close(c->clifd);
    wstrFree(c->ip);
    msgFree(c->req_buf);
    freeList(c->conns);
    if (listLength(ClientPool) > 100) {
        appendToListTail(ClientPool, c);
    } else {
        wfree(c);
    }
}

void finishHandle(struct conn *c)
{
    struct client *client = c->client;
    if (c->protocol_data)
        client->protocol->freeProtocolData(c->protocol_data);
    if (client->protocol)
        c->protocol_data = client->protocol->initProtocolData();
    removeListNode(client->conns, c->node);
    if (!listLength(client->conns))
        msgClean(client->req_buf);
}

int clientSendPacketList(struct client *c)
{
    struct sendPacket *packet = NULL;
    size_t allsend = 0;
    struct listNode *node = NULL;
    struct slice *data;
    while (listLength(c->send_queue)) {
        ssize_t nwritten = 0;
        node = listFirst(c->send_queue);
        packet = listNodeValue(node);
        if (packet->type != SLICE)
            ASSERT(0);

        data = &packet->target.slice;
        while (data->len != 0) {
            nwritten = writeBulkTo(c->clifd, data);
            if (nwritten <= 0)
                break;
            data->len -= nwritten;
            data->data += nwritten;
            allsend += nwritten;
        }

        if (nwritten == -1) {
            setClientUnvalid(c);
            break;
        }

        if (nwritten == 0) {
            break;
        }
        removeListNode(c->send_queue, node);
    }
    return (int)allsend;
}

int sendFileByCopy(struct conn *c, int fd, off_t len, off_t offset)
{
    off_t send = offset;
    int ret;
    if (fd < 0) {
        return WHEAT_WRONG;
    }
    if (len == 0) {
        return WHEAT_WRONG;
    }
    size_t unit_read = Server.max_buffer_size < WHEAT_MAX_BUFFER_SIZE ?
        Server.max_buffer_size/20 : WHEAT_MAX_BUFFER_SIZE/20;
    char ctx[unit_read];
    struct slice slice;
    sliceTo(&slice, (uint8_t *)ctx, unit_read);
    while (send < len) {
        int nread;
        lseek(fd, send, SEEK_SET);
        nread = readBulkFrom(fd, &slice);
        if (nread <= 0)
            return WHEAT_WRONG;
        send += nread;
        ret = sendClientData(c->client, &slice);
        if (ret == -1)
            return WHEAT_WRONG;
    }
    return WHEAT_OK;
}

int sendClientFile(struct conn *c, int fd, off_t len)
{
    int send = 0;
    int ret = WHEAT_OK;
    while (!isClientNeedSend(c->client) && send != len) {
        send += portable_sendfile(c->client->clifd, fd, send, len);
        if (send == -1)
            return WHEAT_WRONG;
    }
    if (send != len) {
        ret = sendFileByCopy(c, fd, len, send);
    }
    return ret;
}

struct client *buildConn(char *ip, int port, struct protocol *p)
{
    struct client *c = NULL;
    int fd = wheatTcpConnect(Server.neterr, ip, port);
    if (fd == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Unable to connect to redis %s:%d: %s %s",
            ip, port, strerror(errno), Server.neterr);
        return NULL;
    }
    if (wheatNonBlock(Server.neterr, fd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set nonblock %d failed: %s", fd, Server.neterr);
        return NULL;
    }
    c = createClient(fd, ip, port, p);
    return c;
}

int sendClientData(struct client *c, struct slice *s)
{
    return WorkerProcess->worker->sendData(c, s);
}

int registerClientRead(struct client *c)
{
    return WorkerProcess->worker->registerRead(c);
}
