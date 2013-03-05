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
    struct configuration *conf = getConfiguration("prealloc-client");
    int i = 0;
    ASSERT(conf);
    ASSERT(l);
    for (; i < conf->target.val; ++i) {
        struct client *c = malloc(sizeof(*c));
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
    free(worker->stat);
    free(worker);
}

struct client *createClient(int fd, char *ip, int port, struct protocol *p)
{
    struct client *c;
    struct listNode *node;
    if ((node = listFirst(ClientPool)) == NULL) {
        c = malloc(sizeof(*c));
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
    c->buf = wstrEmpty();
    c->res_buf = createList();
    listSetFree(c->res_buf, (void (*)(void *))wstrFree);
    c->should_close = 0;
    c->valid = 1;
    ASSERT(c->protocol_data);
    return c;
}

void freeClient(struct client *c)
{
    close(c->clifd);
    wstrFree(c->ip);
    wstrFree(c->buf);
    freeList(c->res_buf);
    c->protocol->freeProtocolData(c->protocol_data);
    if (listLength(ClientPool) > 100) {
        appendToListTail(ClientPool, c);
    } else {
        free(c);
    }
}

void resetProtocol(struct client *c)
{
    if (c->protocol_data)
        c->protocol->freeProtocolData(c->protocol_data);
    c->protocol_data = c->protocol->initProtocolData();
}

int clientSendPacketList(struct client *c)
{
    struct listNode *node = NULL;
    struct listIterator *iter = listGetIterator(c->res_buf, START_HEAD);
    size_t allsend = 0;
    while ((node = listNext(iter)) != NULL) {
        wstr packet = listNodeValue(node);
        size_t bufpos = 0, totallen = wstrlen(packet);
        ssize_t nwritten = 0;

        while (bufpos < totallen) {
            nwritten = writeBulkTo(c->clifd, &packet);
            if (nwritten <= 0)
                break;
            bufpos += nwritten;
            allsend += nwritten;
        }

        if (nwritten == -1) {
            setClientUnvalid(c);
            break;
        }

        if (nwritten == 0) {
            break;
        }
        removeListNode(c->res_buf, node);
    }
    freeListIterator(iter);
    return (int)allsend;
}


