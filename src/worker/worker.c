// Worker process base module
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "../wheatserver.h"
#include "worker.h"

struct workerProcess *WorkerProcess = NULL;

#define WHEAT_CLIENT_MAX      1000

// ========= Statistic Cache ===============
// Cache below stat field avoid too much query on StatItems
static struct statItem *StatBufferSize = NULL;
static struct statItem *StatTotalRequest = NULL;
static struct statItem *StatFailedRequest = NULL;
static struct statItem *StatRunTime = NULL;

enum packetType {
    SLICE = 1,
    FILE_DESCRIPTION = 2,
};

struct fileWrapper {
    int fd;
    off_t off;
    size_t len;
};

struct sendPacket {
    enum packetType type;
    union {
        struct slice slice;
        struct fileWrapper file;
    } target;
};

struct callback {
    void (*func)(void *item);
    void *data;
};

static struct list *FreeClients = NULL;
static struct list *Clients = NULL;
static struct statItem *StatTotalClient = NULL;

// Static fucntion declaretion
static void handleRequest(struct evcenter *center, int fd, void *data, int mask);
static void connDealloc(struct conn *c);
static void freeSendPacket(struct sendPacket *p);
static void callbackCall(void *data);

// ==================================================================
// ======================= Client Implemation =======================
// ==================================================================

static void clientsCron()
{
    long idletime;
    unsigned long numclients;
    unsigned long iteration;
    struct client *c;
    struct listNode *node;

    numclients = listLength(Clients);
    iteration = numclients < 50 ? numclients : numclients / 10;
    while (listLength(Clients) && iteration--) {
        node = listFirst(Clients);
        c = listNodeValue(node);
        ASSERT(c);

        listRotate(Clients);
        idletime = Server.cron_time.tv_sec - c->last_io.tv_sec;
        if (idletime > Server.worker_timeout) {
            wheatLog(WHEAT_VERBOSE, "Closing idle client %s timeout: %lds",
                    c->name, idletime);
            getStatItemByName("Total timeout client")->val++;
            freeClient(c);
            continue;
        }

        if (!listLength(c->conns))
            msgClean(c->req_buf);
    }
}

struct client *createClient(int fd, char *ip, int port, struct protocol *p)
{
    struct client *c;
    struct listNode *node;

    node = listFirst(FreeClients);
    if (node == NULL) {
        c = wmalloc(sizeof(*c));
    } else {
        c = listNodeValue(node);
        removeListNode(FreeClients, node);
        appendToListTail(Clients, c);
    }
    if (c == NULL)
        return NULL;
    c->clifd = fd;
    c->ip = wstrNew(ip);
    c->port = port;
    c->protocol = p;
    c->conns = createList();
    listSetFree(c->conns, (void (*)(void*))connDealloc);
    c->req_buf = msgCreate(Server.mbuf_size);
    c->is_outer = 1;
    c->should_close = 0;
    c->valid = 1;
    c->pending = NULL;
    c->client_data = NULL;
    c->notify = NULL;
    c->last_io = Server.cron_time;
    c->name = wstrEmpty();

    createEvent(WorkerProcess->center, c->clifd, EVENT_READABLE,
            handleRequest, c);
    getStatVal(StatTotalClient)++;
    return c;
}

void freeClient(struct client *c)
{
    struct listNode *node;

    if (c->notify)
        c->notify(c);
    wstrFree(c->ip);
    wstrFree(c->name);
    msgFree(c->req_buf);
    freeList(c->conns);
    close(c->clifd);
    node = searchListKey(Clients, c);
    deleteEvent(WorkerProcess->center, c->clifd, EVENT_READABLE|EVENT_WRITABLE);
    if (!node)
        ASSERT(0);
    removeListNode(Clients, node);
    if (listLength(FreeClients) <= 120) {
        appendToListTail(FreeClients, c);
    } else {
        wfree(c);
    }
}

void tryFreeClient(struct client *c)
{
    if (isClientValid(c) && (isClientNeedSend(c) || !c->should_close))
        return;
    freeClient(c);
}

int isClientNeedSend(struct client *c)
{
    struct listNode *node;
    struct conn *send_conn;
    if (listLength(c->conns) && isClientValid(c)) {
        node = listFirst(c->conns);
        send_conn = listNodeValue(node);
        if (listLength(send_conn->send_queue) || send_conn->ready_send) {
            return 1;
        }
    }
    return 0;
}

static struct list *createAndFillPool()
{
    int i;
    struct list *l = createList();
    ASSERT(l);
    for (i = 0; i < 120; ++i) {
        struct client *c = wmalloc(sizeof(*c));
        appendToListTail(l, c);
    }
    return l;
}

// ==================================================================
// ========================= Conn Implemation =======================
// ==================================================================

struct conn *connGet(struct client *client)
{
    if (client->pending) {
        ASSERT(client->pending->client);
        return client->pending;
    }
    struct conn *c = wmalloc(sizeof(*c));
    c->client = client;
    c->protocol_data = client->protocol->initProtocolData();
    c->app = c->app_private_data = NULL;
    appendToListTail(client->conns, c);
    c->ready_send = 0;
    c->send_queue = createList();
    listSetFree(c->send_queue, (void(*)(void*))freeSendPacket);
    c->cleanup = arrayCreate(sizeof(struct callback), 2);
    return c;
}

static void connDealloc(struct conn *c)
{
    if (c->protocol_data)
        c->client->protocol->freeProtocolData(c->protocol_data);
    if (c->app_private_data)
        c->app->freeAppData(c->app_private_data);
    arrayEach(c->cleanup, callbackCall);
    arrayDealloc(c->cleanup);
    freeList(c->send_queue);
    wfree(c);
}

void finishConn(struct conn *c)
{
    c->ready_send = 1;
    clientSendPacketList(c->client);
}

void registerConnFree(struct conn *conn, void (*clean)(void*), void *data)
{
    struct callback cleanup;
    cleanup.func = clean;
    cleanup.data = data;
    arrayPush(conn->cleanup, &cleanup);
}

// ==================================================================
// ================== Worker Process IO Support =====================
// ==================================================================

static void freeSendPacket(struct sendPacket *p)
{
    wfree(p);
}

static void callbackCall(void *data)
{
    struct callback *c = data;
    c->func(c->data);
}

static void appendFileToSendQueue(struct conn *conn, int fd, off_t off,
        size_t len)
{
    struct sendPacket *packet = wmalloc(sizeof(*packet));
    if (!packet)
        setClientUnvalid(conn->client);
    packet->type = FILE_DESCRIPTION;
    packet->target.file.fd = fd;
    packet->target.file.off = off;
    packet->target.file.len = len;

    appendToListTail(conn->send_queue, packet);
}

static void appendSliceToSendQueue(struct conn *conn, struct slice *s)
{
    struct sendPacket *packet = wmalloc(sizeof(*packet));
    if (!packet)
        setClientUnvalid(conn->client);
    packet->type = SLICE;
    sliceTo(&packet->target.slice, s->data, s->len);
    appendToListTail(conn->send_queue, packet);
}

// Return value:
// 0: send packet completely
// 1: send packet incompletely
// -1: send packet error client need closed
static int sendPacket(struct client *c, struct sendPacket *packet)
{
    struct slice *data;
    ssize_t nwritten = 0;
    struct fileWrapper *file_wrapper;
    switch (packet->type) {
        case SLICE:
            data = &packet->target.slice;
            while (data->len != 0) {
                nwritten = writeBulkTo(c->clifd, data);
                if (nwritten <= 0)
                    break;
                data->len -= nwritten;
                data->data += nwritten;
            }

            if (nwritten == -1) {
                return -1;
            }

            if (nwritten == 0) {
                return 1;
            }
            break;
        case FILE_DESCRIPTION:
            file_wrapper = &packet->target.file;
            while (file_wrapper->len > 0) {
                nwritten = portable_sendfile(c->clifd, file_wrapper->fd,
                        file_wrapper->off, file_wrapper->len);
                if (nwritten == -1)
                    return -1;
                else if (nwritten == 0) {
                    return 1;
                }
                file_wrapper->off += nwritten;
                file_wrapper->len -= nwritten;
            }
    }
    return 0;
}

void clientSendPacketList(struct client *c)
{
    struct sendPacket *packet;
    struct conn *send_conn;
    struct listNode *node, *node2;
    ssize_t ret;

    while (isClientNeedSend(c)) {
        node = listFirst(c->conns);
        send_conn = listNodeValue(node);

        while (listLength(send_conn->send_queue)) {
            node2 = listFirst(send_conn->send_queue);
            packet = listNodeValue(node2);
            ret = sendPacket(c, packet);
            if (ret == -1) {
                setClientUnvalid(c);
                return ;
            } else if (ret == 1) {
                return ;
            }
            ASSERT(ret == 0);
            removeListNode(send_conn->send_queue, node2);
        }
        if (send_conn->ready_send)
            removeListNode(c->conns, node);
    }
}

int sendClientFile(struct conn *c, int fd, off_t len)
{
    int send = 0;
    appendFileToSendQueue(c, fd, send, len-send);
    return WorkerProcess->worker->sendData(c);
}

int sendClientData(struct conn *c, struct slice *s)
{
    if (!s->len)
        return WHEAT_OK;
    appendSliceToSendQueue(c, s);
    return WorkerProcess->worker->sendData(c);
}

// ==================================================================
// ============= Worker Process Connection Functions ================
// ==================================================================

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
    if (!c) {
        close(fd);
        return NULL;
    }
    c->is_outer = 0;
    return c;
}

static void handleRequest(struct evcenter *center, int fd, void *data, int mask)
{
    struct client *client;
    struct conn *conn;
    ssize_t nread, ret;
    struct timeval start, end;
    long time_use;
    struct slice slice;
    size_t parsed;

    parsed = 0;
    ret = 0;
    client = data;

    gettimeofday(&start, NULL);
    nread = WorkerProcess->worker->recvData(client);
    if (!isClientValid(client)) {
        freeClient(client);
        return ;
    }

    if (msgGetSize(client->req_buf) > getStatVal(StatBufferSize)) {
        getStatVal(StatBufferSize) = msgGetSize(client->req_buf);
    }

    while (msgCanRead(client->req_buf)) {
        conn = connGet(client);

        msgRead(client->req_buf, &slice);
        ret = client->protocol->parser(conn, &slice, &parsed);

        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_NOTICE, "parse data failed");
            msgSetReaded(client->req_buf, 0);
            setClientUnvalid(client);
            break;
        } else if (ret == WHEAT_OK) {
            msgSetReaded(client->req_buf, parsed);
            getStatVal(StatTotalRequest)++;
            client->pending = NULL;
            ret = client->protocol->spotAppAndCall(conn);
            if (ret != WHEAT_OK) {
                getStatVal(StatFailedRequest)++;
                client->should_close = 1;
                wheatLog(WHEAT_NOTICE, "app failed");
                break;
            }
        } else if (ret == 1) {
            client->pending = conn;
            msgSetReaded(client->req_buf, parsed);
            continue;
        }
    }
    tryFreeClient(client);
    gettimeofday(&end, NULL);
    time_use = 1000000 * (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec);
    getStatVal(StatRunTime) += time_use;
}

static void acceptClient(struct evcenter *center, int fd, void *data, int mask)
{
    char ip[46];
    struct client *c;
    int cport, cfd;

    cfd = wheatTcpAccept(Server.neterr, fd, ip, &cport);
    if (cfd == NET_WRONG) {
        if (errno != EAGAIN)
            wheatLog(WHEAT_WARNING, "Accepting client connection failed: %s", Server.neterr);
        return;
    }
    wheatNonBlock(Server.neterr, cfd);
    wheatCloseOnExec(Server.neterr, cfd);

    c = createClient(cfd, ip, cport, WorkerProcess->protocol);
}

// ==================================================================
// ================== Worker Process Implemation ====================
// ==================================================================

void initWorkerProcess(struct workerProcess *worker, char *worker_name)
{
    struct configuration *conf;
    int i;
    struct app *app;
    struct moduleAttr *module;

    if (Server.stat_fd != 0)
        close(Server.stat_fd);
    setProctitle(worker_name);
    worker->pid = getpid();
    worker->ppid = getppid();
    worker->alive = 1;
    worker->start_time = Server.cron_time;
    worker->worker = spotWorker(worker_name);
    worker->master_stat_fd = 0;
    ASSERT(worker->worker);
    worker->stats = NULL;
    initWorkerSignals();
    worker->center = eventcenterInit(WHEAT_CLIENT_MAX);
    if (!worker->center) {
        wheatLog(WHEAT_WARNING, "eventcenter_init failed");
        halt(1);
    }
    if (createEvent(worker->center, Server.ipfd, EVENT_READABLE, acceptClient,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        halt(1);
    }

    // It may nonblock after fork???
    if (wheatNonBlock(Server.neterr, Server.ipfd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set nonblock %d failed: %s", Server.ipfd, Server.neterr);
        halt(1);
    }

    module = NULL;
    conf = getConfiguration("protocol");
    if (conf->target.ptr) {
        module = getModule(PROTOCOL, conf->target.ptr);
        if (module && getProtocol(module)->initProtocol() == WHEAT_WRONG) {
            wheatLog(WHEAT_WARNING, "init protocol failed");
            halt(1);
        }
    }
    if (!module) {
        wheatLog(WHEAT_WARNING, "find protocol %s failed", conf->target.ptr);
        halt(1);
    }
    worker->protocol = getProtocol(module);

    StatTotalClient = getStatItemByName("Total client");
    gettimeofday(&Server.cron_time, NULL);
    if (worker->worker->setup)
        worker->worker->setup();
    WorkerProcess->refresh_time = Server.cron_time.tv_sec;

    FreeClients = createAndFillPool();
    Clients = createList();
    StatBufferSize = getStatItemByName("Max buffer size");
    StatTotalRequest = getStatItemByName("Total request");
    StatFailedRequest = getStatItemByName("Total failed request");
    StatRunTime = getStatItemByName("Worker run time");

    worker->apps = arrayCreate(sizeof(struct app*), 3);
    if (!worker->apps) {
        wheatLog(WHEAT_WARNING, "array create failed");
        halt(1);
    }

    getAppsByProtocol(worker->apps, worker->protocol);
    for (i = 0; i < narray(worker->apps); i++) {
        app = *(struct app**)arrayIndex(worker->apps, i);
        if (app->initApp(worker->protocol) == WHEAT_WRONG) {
            wheatLog(WHEAT_WARNING, "init app failed %s", getModuleName(APP, app));
            halt(1);
        }
    }

    sendStatPacket(WorkerProcess);
}

void freeWorkerProcess(void *w)
{
    struct workerProcess *worker = w;
    if (worker->stats)
        wfree(worker->stats);
    wfree(worker);
}

static inline void appCronRun(void *t)
{
    struct app **app = t;
    if ((*app)->appCron)
        (*app)->appCron();
}

// workerProcessCron is the cron of worker process, it must be called before
// worker process initialized.
//
// `fake_func` is used by spawnFakeWorker and call it every cron.
void workerProcessCron(void (*fake_func)(void *data), void *data)
{
    static long long max_cron_interval = 0;

    struct timeval nowval;
    long long interval;
    int refresh_seconds;
    void (*worker_cron)();

    refresh_seconds = Server.stat_refresh_seconds;
    worker_cron = WorkerProcess->worker->cron;
    while (WorkerProcess->alive) {
        arrayEach(WorkerProcess->apps, appCronRun);

        if (worker_cron)
            worker_cron();
        if (fake_func)
            fake_func(data);
        processEvents(WorkerProcess->center, WHEATSERVER_CRON_MILLLISECONDS);
        if (WorkerProcess->ppid != getppid()) {
            wheatLog(WHEAT_NOTICE, "parent change, worker shutdown");
            WorkerProcess->alive = 0;
        }
        clientsCron();

        if (Server.cron_time.tv_sec - WorkerProcess->refresh_time > refresh_seconds) {
            sendStatPacket(WorkerProcess);
            WorkerProcess->refresh_time = Server.cron_time.tv_sec;
        }

        // Get the max worker cron interval for statistic info
        gettimeofday(&nowval, NULL);
        interval = getMicroseconds(nowval) - getMicroseconds(Server.cron_time);
        if (interval > max_cron_interval) {
            max_cron_interval = interval;
            getStatValByName("Max worker cron interval") = interval;
        }
        Server.cron_time = nowval;
    }

    // Stop accept new client
    deleteEvent(WorkerProcess->center, Server.ipfd, EVENT_READABLE|EVENT_WRITABLE);
    WorkerProcess->refresh_time = Server.cron_time.tv_sec;
    while (Server.cron_time.tv_sec - WorkerProcess->refresh_time < Server.graceful_timeout) {
        processEvents(WorkerProcess->center, WHEATSERVER_CRON_MILLLISECONDS);
        gettimeofday(&Server.cron_time, NULL);
    }
}
