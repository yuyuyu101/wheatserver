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

void initWorkerProcess(char *worker_name)
{
    if (Server.stat_fd != 0)
        close(Server.stat_fd);
    WorkerProcess->pid = getpid();
    WorkerProcess->ppid = getppid();
    nonBlockCloseOnExecPipe(&WorkerProcess->pipe_readfd, &WorkerProcess->pipe_writefd);
    wheatNonBlock(Server.neterr, Server.ipfd); //after fork, it seems reset nonblock
    WorkerProcess->alive = 1;
    WorkerProcess->worker_name = worker_name;
    WorkerProcess->worker = spotWorker(worker_name);
    initWorkerStat();
    initWorkerSignals();
    WorkerProcess->worker->cron();
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
