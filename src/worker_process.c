#include "wheatserver.h"

struct workerProcess *WorkerProcess = NULL;

static struct worker WorkerTable[] = {
    {"SyncWorker", setupSync, syncWorkerCron, syncSendData, syncRecvData},
    {"AsyncWorker", setupAsync, asyncWorkerCron, asyncSendData, asyncRecvData}
};

/* ========== Worker Area ========== */

static struct list *ClientPool = NULL;

struct worker *spotWorker(char *worker_name)
{
    int i;
    for (i = 0; i < sizeof(WorkerTable)/sizeof(struct worker); i++) {
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

struct client *createClient(int fd, char *ip, int port, struct protocol *p, struct app *app)
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

    c->protocol = p;
    c->protocol_data = p->initProtocolData();
    c->app = app;
    c->app_private_data = app->initAppData();
    c->buf = wstrEmpty();
    c->res_buf = wstrEmpty();
    c->last_io = Server.cron_time;
    ASSERT(c->protocol_data && c->app_private_data);
    return c;
}

void freeClient(struct client *c)
{
    close(c->clifd);
    wstrFree(c->ip);
    wstrFree(c->buf);
    wstrFree(c->res_buf);
    c->protocol->freeProtocolData(c->protocol_data);
    c->app->freeAppData(c->app_private_data);
    if (listLength(ClientPool) > 100) {
        appendToListTail(ClientPool, c);
    } else {
        free(c);
    }
}
