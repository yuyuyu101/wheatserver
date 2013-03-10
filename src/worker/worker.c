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
    c->protocol_data = p->initProtocolData();
    c->protocol = p;
    c->app_private_data = NULL;
    c->app = NULL;
    c->err = NULL;
    c->req_buf = msgCreate(Server.mbuf_size);
    c->res_buf = msgCreate(Server.mbuf_size);
    c->should_close = 0;
    c->valid = 1;
    ASSERT(c->protocol_data);
    return c;
}

void freeClient(struct client *c)
{
    close(c->clifd);
    wstrFree(c->ip);
    msgFree(c->req_buf);
    msgFree(c->res_buf);
    c->protocol->freeProtocolData(c->protocol_data);
    if (listLength(ClientPool) > 100) {
        appendToListTail(ClientPool, c);
    } else {
        wfree(c);
    }
}

void resetClientCtx(struct client *c)
{
    if (c->protocol_data)
        c->protocol->freeProtocolData(c->protocol_data);
    c->protocol_data = c->protocol->initProtocolData();
    msgClean(c->req_buf);
}

int clientSendPacketList(struct client *c)
{
    struct slice data;
    size_t allsend = 0;
    do {
        allsend = 0;
        msgRead(c->res_buf, &data);
        ssize_t nwritten = 0;

        while (data.len != 0) {
            nwritten = writeBulkTo(c->clifd, &data);
            if (nwritten <= 0)
                break;
            data.len -= nwritten;
            data.data += nwritten;
            allsend += nwritten;
        }

        msgSetReaded(c->res_buf, allsend);
        if (nwritten == -1) {
            setClientUnvalid(c);
            break;
        }

        if (nwritten == 0) {
            break;
        }
    } while(allsend == data.len);
    return (int)allsend;
}

int sendFileByCopy(struct client *c, int fd, off_t len, off_t offset)
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
        ret = WorkerProcess->worker->sendData(c, &slice);
        if (ret == -1)
            return WHEAT_WRONG;
    }
    return WHEAT_OK;
}

int sendClientFile(struct client *c, int fd, off_t len)
{
    int send = 0;
    int ret = WHEAT_OK;
    while (!isClientNeedSend(c) && send != len) {
        send += portable_sendfile(c->clifd, fd, send, len);
        if (send == -1)
            return WHEAT_WRONG;
    }
    if (send != len) {
        ret = sendFileByCopy(c, fd, len, send);
    }
    return ret;
}

void *clientPalloc(struct client *c, size_t size)
{
    return slabAlloc(c->pool, size);
}
