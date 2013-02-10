#include "wheatserver.h"

struct workerProcess *WorkerProcess = NULL;

struct worker workerTable[] = {
    {"SyncWorker", setupSync, syncWorkerCron, syncSendData, syncRecvData}
};

/* ========== Worker Area ========== */

struct worker *spotWorker(char *worker_name)
{
    int i;
    for (i = 0; i < sizeof(workerTable)/sizeof(workerTable); i++) {
        if (strcmp(workerTable[i].name, worker_name) == 0) {
            return &workerTable[i];
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

void initWorkerProcess(struct workerProcess *worker, char *worker_name)
{
    if (Server.stat_fd != 0)
        close(Server.stat_fd);
    worker->pid = getpid();
    worker->ppid = getppid();
    worker->alive = 1;
    worker->start_time = time(NULL);
    worker->worker_name = worker_name;
    worker->worker = spotWorker(worker_name);
    worker->stat = initWorkerStat(0);
    initWorkerSignals();
}

void freeWorkerProcess(void *w)
{
    struct workerProcess *worker = w;
    free(worker->stat);
    free(worker);
}

struct client *initClient(int fd, char *ip, int port, struct protocol *p, struct app *app)
{
    struct client *c = malloc(sizeof(struct client));
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
    if (c->protocol_data && c->app_private_data)
        return c;
    free(c);
    return NULL;
}

void freeClient(struct client *c)
{
    wstrFree(c->buf);
    wstrFree(c->res_buf);
    c->protocol->freeProtocolData(c->protocol_data);
    c->app->freeAppData(c->app_private_data);
    free(c);
}
