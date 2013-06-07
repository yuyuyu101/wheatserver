// Worker process base module
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_WORKER_PROCESS_H
#define WHEATSERVER_WORKER_PROCESS_H

#include <unistd.h>
#include <setjmp.h>
#include <sys/time.h>

#include "../slice.h"
#include "../wstr.h"
#include "../slab.h"
#include "mbuf.h"

#define WORKER_BOOT_ERROR 3
#define WHEATSERVER_REQ_MAXLEN 8*1024

// Workerprocess's Flow
// =====================
//
// 0. setup filling workerProcess members
// 1. accept connection and init client
// 2. recognize protocol(http or pop3 etc...)
// 3. read big bulk data from socket (sync or async)
// 4. call protocol parser
// 5. call app constructor and pass the protocol parsed data
// 6. construct response and send to
//
// Worker's Duty:
// =====================
// provide with send and receive api and refresh client's last_io field

// The structure represented for worker process, each worker process only have
// one workerProcess.
//
// `protocol`: the protocol module this worker process used to parse request
// packet.
// `apps`: the application modules belong to `protocol`
// `ppid`: the pid of master process, is used to detect whether master process
// is alive.
// `alive`: when received signal like SIGKILL will set `alive` to 0, worker
// process will exit gracefully(stop accept new client and handle old requests).
// `worker`: the worker module which provide with IO methods and others
// `stats`: the statistic items array
// `center`: the event-driven center used to manage events
// `master_stat_fd`: the file description used to send statistic packets to
// mastser
// `refresh_time`: In worker process side, `refresh_time` as last statistic
// packet sended time used to decide whether this cron should send statistic
// packet. In master process side, `refresh_time` is used to indicate the last
// received time of this worker process
// `start_time`: the time of worker process started
struct workerProcess {
    struct protocol *protocol;
    struct array *apps;
    pid_t pid;
    pid_t ppid;
    int alive;

    struct worker *worker;

    struct array *stats;
    struct evcenter *center;
    int master_stat_fd;
    time_t refresh_time;
    struct timeval start_time;
};

struct client;
struct conn;

// Protocol Interface
// ==================
// Each protocol module must implement `parser`, `spotAppAndCall`.
//
// `attr`: the module attributes(see moduleAttr below)
// `spotAppAndCall`: according to parsed results to decide call application
// which matched.
// `parser`: parse data in `s`. `nparsed` is value-argument and indicate
// parsed bytes. return 0 imply parser success, return 1 means data continued
// and return -1 means parse error.
// `initProtocolData`: implement protocol data attached to each request,
// parsed data useful can store to it.
// `initProtocol`: used to setup protocol module
struct protocol {
    int (*spotAppAndCall)(struct conn *);
    int (*parser)(struct conn *conn, struct slice *s, size_t *nparsed);
    void *(*initProtocolData)();
    void (*freeProtocolData)(void *ptcol_data);
    int (*initProtocol)();
    void (*deallocProtocol)();
};

// Worker Interface
// ==================
// Each worker module must implement `sendData` and `recvData`.
//
// `setup`: setup worker module
// `cron`: worker module cron function, it will be called every heart interval
// `sendData`: implemente way to support send data in client buffer
// `recvData`: implemente way to support receive data into client buffer
struct worker {
    void (*setup)();
    void (*cron)();
    /* Send data in buffer which `buf` points
     * return -1 imply send data failed and the remaining length
     * of bufmeans the length of data not sended.
     * return others means the length of data.
     *
     * Caller will lose ownership of `data` in order to avoid big copy,
     * sendData will free `data`
     */
    int (*sendData)(struct conn *);
    int (*recvData)(struct client *);
};

// Application Interface
// =====================
// Each application module must implement `appCall`.
//
// `appCron`: application cron function and will be called each heart interval
// `appCall`: main application logic implementation.
//  Return WHEAT_OK means all is ok and return WHEAT_WRONG means something
//  wrong inside and worker will call `deallocApp` of this app. It's important
//  to decide whether return WHEAT_WRONG when error occured. In general, if
//  error only affect this request and not affect app itself, application module
//  is hoped not to return WHEAT_WRONG.
//  `initApp`: initialize application module
//  `deallocApp`: `deallocApp` must clean up all data alloced
//  `initAppData`: used to store application level data
//  `is_init`: indicate whether application initialized, because it may get
//  WHEAT_WRONG calling `appCall`.
struct app {
    char *proto_belong;
    void (*appCron)();
    int (*appCall)(struct conn *, void *arg);
    int (*initApp)(struct protocol *);
    void (*deallocApp)();
    void *(*initAppData)(struct conn *);
    void (*freeAppData)(void *app_data);
    int is_init;
};

// Conn structure
// ==============
//
// *conn* is important structure to support worker process. It will created when
// new request received and released when response correspond to this requests
// sent. In other words, conn's lifecycle is between a new request create and
// response sent. Modules mainly use conn as identifier to distinguish different
// requests. But there is not always follow this mode.
// For example, proxy module may build its own connection to backend servers.
// In this way, conn is get by `connGet` API' every time needed to send requests
// to backend. And released when this conn is sent. In this scene, conn
// isn't get through by request. When backend server's response is received, new
// conn is created by worker transparently. Proxy module should destroy it
// directly because there is no need to send response to backend.
// As you can see, conn is created when new request received or you need send
// request to other servers and is released when content is sent.
//
// `client`: conn's owner, represented the peer socket
// `protocol_data`: attached by protocol module and store parsed relevant data
// `app_private_data`: attached by application module
// `send_queue`: transparent to modules and store pieces of packets need sent
// `ready_send`: the name may disturb you. When conn isn't needed and call
// finishConn, this field will be set to 1, and worker cron will remove this
// conn. In a word, it's a indicator show application whether need this conn
// `cleanup`: in order to reach no-copy goal, application may save buffer wait
// to be sent. Application module can make buffer rely on special conn, it may
// like garbage collection mechanism.
// `next`: the next conn below to `client`
struct conn {
    struct client *client;
    void *protocol_data;
    struct app *app;
    void *app_private_data;
    struct list *send_queue;
    int ready_send;
    struct array *cleanup;
    struct conn *next;
};

// Client Structure
// ================
//
// Client is a entity represents peer computer. Client will be created when new
// connection accepted. When new packet received, client will create new conn
// and manage it. When conn is finished(call finishConn), client will traverse
// `conns` fields and send packets in ordering.
//
// `last_io`: the last send or receive time
// `name`: the client name, it always used by application to debug or show
// information attach client
// `protocol`: the protocol attached
// `pending`: when packet received incompletely and will set this conn pending.
// The next packet received will continuously use pending conn
// `conns`: the list of conn belong to this client
// `req_buf`: the request packet manage unit in order to reach no-copy(see mbuf.h)
// `client_data`: used by application and store data attaches client not conn
// `notify`: set notify function to give notice to application if application
// module keep some objects attach client
// `notify_data`: used by `notify`
// `is_outer`: client is divided into two type: from outer or connect to backend
// server. Modules can use this attribute as indicator to distinguish client if
// needed
// `shoul_close`: Modules can set this client need to be closed if content
// needed sent is empty, such as HTTP's keep-live field
// `valid`: used by worker process intern, when read or write to `clifd`
// resulted in error, `valid` is set to 0. Any IO function will failed if
// valid is 0
struct client {
    int clifd;
    wstr ip;
    int port;
    struct timeval last_io;
    wstr name;
    struct protocol *protocol;
    struct conn *pending;
    struct list *conns;
    struct msghdr *req_buf;
    void *client_data;
    void (*notify)(struct client*);
    void *notify_data;

    unsigned is_outer:1;
    unsigned should_close:1; // Used to indicate whether closing client
    unsigned valid:1;        // Intern: used to indicate client fd is unused and
                             // need closing, only used by worker IO methods when
                             // error happended
};

#define WHEAT_WORKERS    2
extern struct workerProcess *WorkerProcess;

void initWorkerProcess(struct workerProcess *worker, char *worker_name);
void freeWorkerProcess(void *worker);
void workerProcessCron(void (*fake_func)(void *data), void *data);

//==================================================================
//========================== Client operation ======================
//==================================================================

struct client *createClient(int fd, char *ip, int port, struct protocol *p);
void freeClient(struct client *);
void tryFreeClient(struct client *c);
int sendClientFile(struct conn *c, int fd, off_t len);
int sendClientData(struct conn *c, struct slice *s);
int isClientNeedSend(struct client *);
// Used by worker module only
void clientSendPacketList(struct client *c);

#define isClientValid(c)                   ((c)->valid)
#define isClientNeedParse(c)               (msgCanRead(c)->req_buf))
#define isOuterClient(c)                   ((c)->is_outer == 1)
#define setClientUnvalid(c)                ((c)->valid = 0)
#define setClientClose(c)                  ((c)->client->should_close = 1)
#define refreshClient(c)                   ((c)->last_io = (Server.cron_time))
#define setClientName(c, n)                ((c)->name = wstrCat(c->name, (n)))
#define setClientFreeNotify(c, func)       ((c)->notify = (func))

//==================================================================
//========================== Conn operation ========================
//==================================================================

struct client *buildConn(char *ip, int port, struct protocol *p);
void finishConn(struct conn *c);
struct conn *connGet(struct client *client);
void registerConnFree(struct conn*, void (*)(void*), void *data);

#define getConnIP(c)                       ((c)->client->ip)
#define getConnPort(c)                     ((c)->client->port)

#endif
