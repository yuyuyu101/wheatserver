#ifndef WHEATSERVER_WORKER_PROCESS_H
#define WHEATSERVER_WORKER_PROCESS_H

#include <unistd.h>
#include <setjmp.h>
#include "../wheatserver.h"
#include "mbuf.h"
#include "../slab.h"

#define WORKER_BOOT_ERROR 3
#define WHEATSERVER_REQ_MAXLEN 8*1024

struct workerProcess {
    pid_t pid;
    pid_t ppid;
    int alive;
    struct timeval start_time;

    char *worker_name;
    struct worker *worker;

    struct statItem *stat;
    int master_stat_fd;
    // In worker process side, last statistic packet sended time, use
    // `refresh_time` to decide this cron should send statistic packet
    // In master process side, `refresh_time` is used to indicate the last
    // send time of this worker process
    time_t refresh_time;
};

struct client;
struct conn;

struct protocol {
    char *name;
    int (*spotAppAndCall)(struct conn *);
    /* `parser` parse `data` and assign protocol data to client
     * if return 0 imply parser success,
     * return 1 means data continued and return -1 means parse
     * error. `nparsed` is value-argument and indicate parsed bytes*/
    int (*parser)(struct conn *, struct slice *, size_t *nparsed);
    void *(*initProtocolData)();
    void (*freeProtocolData)(void *ptcol_data);
    int (*initProtocol)();
    void (*deallocProtocol)();
};

struct worker {
    char *name;
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
    int (*sendData)(struct conn *, struct slice*);
    int (*registerRead)(struct client *);
    void (*unregisterRead)(struct client *);
};

struct app {
    char *proto_belong;
    char *name;
    void (*appCron)();
    /* Return WHEAT_OK means all is ok and return WEHAT_WRONG means something
     * wrong inside and worker will clean this connection */
    int (*appCall)(struct conn *, void *arg);
    int (*initApp)(struct protocol *);
    void (*deallocApp)();
    void *(*initAppData)(struct conn *);
    void (*freeAppData)(void *app_data);
    int is_init;
};

struct cleanup {
    void (*func)(void *data);
    void *clean_data;
};

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

enum packetType {
    SLICE = 1,
};

struct sendPacket {
    enum packetType type;
    union {
        struct slice slice;
    } target;
};

struct client {
    int clifd;
    wstr ip;
    int port;
    struct timeval last_io;
    char *err;
    struct protocol *protocol;
    struct conn *pending;
    struct list *conns;
    struct msghdr *req_buf;
    void *client_data;       // Only used by app
    void (*notifyFree)(struct client *, void *);
    void *notify_data;

    unsigned is_outer:1;
    unsigned should_close:1; // Used to indicate whether closing client
    unsigned valid:1;        // Intern: used to indicate client fd is unused and
                             // need closing, only used by worker IO methods when
                             // error happended
};

#define WHEAT_WORKERS    2
extern struct workerProcess *WorkerProcess;
extern struct worker WorkerTable[];
extern struct protocol ProtocolTable[];
extern struct app AppTable[];

/* modify attention. Worker, Protocol, Applicantion interface */
void initWorkerProcess(struct workerProcess *worker, char *worker_name);
void freeWorkerProcess(void *worker);
void workerProcessCron();
void appCron();

struct client *createClient(int fd, char *ip, int port, struct protocol *p);
void freeClient(struct client *);
void finishConn(struct conn *c);
int clientSendPacketList(struct client *c);
int sendClientFile(struct conn *c, int fd, off_t len);
struct client *buildConn(char *ip, int port, struct protocol *p);
int initAppData(struct conn *);
int registerClientRead(struct client *c);
void unregisterClientRead(struct client *c);
int sendClientData(struct conn *c, struct slice *s);
struct conn *connGet(struct client *client);
void appendSliceToSendQueue(struct conn *conn, struct slice *s);
int isClientNeedSend(struct client *);
void registerConnFree(struct conn*, void (*)(void*), void *data);

#define isClientValid(c)                   ((c)->valid)
#define setClientUnvalid(c)                ((c)->valid = 0)
#define isClientNeedParse(c)               (msgCanRead(c)->req_buf))
#define refreshClient(c, t)                ((c)->last_io = (t))
#define getConnIP(c)                       ((c)->client->ip)
#define getConnPort(c)                     ((c)->client->port)
#define setClientClose(c)                  ((c)->client->should_close = 1)
#define isOuterClient(c)                   ((c)->is_outer == 1)
#define setClientFreeNotify(c, func, data) \
    (c)->notifyFree = func; \
     (c)->notify_data = data

/* worker's flow:
 * 0. setup filling workerProcess members
 * 1. accept connection and init client
 * 2. recognize protocol(http or pop3 etc...)
 * 3. read big bulk data from socket (sync or async)
 * 4. call protocol parser
 * 5. call app constructor and pass the protocol parsed data
 * 6. construct response and send to
 *
 * worker's duty:
 * 1. provide with send and receive api and refresh client's last_io field
 * 2. if parent changed, worker must detect and exit
 * 3. if alive == 0, worker must exit
 * 4. send worker status every refresh time
 * 5. guarantee closing client in Server.worker_timeout
 * 6. support pipeline processing must reset client use readyClient()
 * */
struct protocol *spotProtocol(char *ip, int port, int fd);
struct app *spotAppInterface();

#endif
