#ifndef WHEATSERVER_WORKER_PROCESS_H
#define WHEATSERVER_WORKER_PROCESS_H

#include <unistd.h>
#include <setjmp.h>
#include "wstr.h"

#define WORKER_BOOT_ERROR 3
#define WHEATSERVER_REQ_MAXLEN 8*1024

struct workerProcess {
    pid_t pid;
    pid_t ppid;
    int alive;
    time_t start_time;

    jmp_buf jmp;

    char *worker_name;
    struct worker *worker;
    struct workerStat *stat;
};

extern struct workerProcess *WorkerProcess;

struct client;
struct protocol {
    char *name;
    /* buildRequest parse `data` and assign protocol data to client
     * if return 0 imply parser success,
     * return -1 imply parser error,
     * return 1 imply request incomplete */
    int (*spotAppAndCall)(struct client *);
    int (*parser)(struct client *);
    void *(*initProtocolData)();
    void (*freeProtocolData)(void *ptcol_data);
    void (*initProtocol)();
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
    int (*sendData)(struct client *, wstr data);
    int (*recvData)(struct client *);
};

struct app {
    char *proto_belong;
    char *name;
    /* Return WHEAT_OK means all is ok and return WEHAT_WRONG means something
     * wrong inside and worker will clean this connection */
    int (*appCall)(struct client *, void *arg);
    void (*initApp)();
    void (*deallocApp)();
    void *(*initAppData)(struct client *);
    void (*freeAppData)(void *app_data);
    int is_init;
};

struct client {
    int clifd;
    wstr ip;
    int port;
    struct protocol *protocol;
    void *protocol_data;
    struct app *app;
    void *app_private_data;
    time_t last_io;
    int should_close;
    int valid;       // Intern: used to indicate client fd is unused and
                     // need closing, only used by worker IO methods.
    wstr buf;
    struct list *res_buf;
};


/* modify attention. Worker, Protocol, Applicantion interface */
void initWorkerProcess(struct workerProcess *worker, char *worker_name);
void freeWorkerProcess(void *worker);
void workerProcessCron();
struct client *createClient(int fd, char *ip, int port, struct protocol *p);
void freeClient(struct client *);
void resetProtocol(struct client *c);
int clientSendPacketList(struct client *c);

#define isClientValid(c)    (c)->valid
#define setClientUnvalid(c) (c)->valid = 0
#define isClientNeedSend(c) (listFirst((c)->res_buf))
#define refreshClient(c, t) ((c)->last_io = (t))

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


/* Sync worker Area */
void setupSync();
void syncWorkerCron();
int syncSendData(struct client *c, wstr data); // pass `data` ownership to
int syncRecvData(struct client *c);

/* Async worker Area */
void setupAsync();
void asyncWorkerCron();
int asyncSendData(struct client *c, wstr data); // pass `data` ownership to
int asyncRecvData(struct client *c);

#endif
