#ifndef _WORKER_H
#define _WORKER_H

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
    int (*parser)(struct client *);
    void *(*initProtocolData)();
    void (*freeProtocolData)(void *ptcol_data);
    void (*initProtocol)();
    void (*deallocProtocol)();
};

extern struct protocol protocolTable[];

struct worker {
    char *name;
    void (*setup)();
    void (*cron)();
    /* Send data in buffer which `buf` points
     * return -1 imply send data failed and the remaining length
     * of bufmeans the length of data not sended.
     * return others means the length of data.
     */
    int (*sendData)(struct client *);
    int (*recvData)(struct client *);
};

extern struct worker workerTable[];

struct app {
    char *name;
    int (*constructor)(struct client *);
    void (*initApp)();
    void (*deallocApp)();
    void *(*initAppData)();
    void (*freeAppData)(void *app_data);
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

    wstr buf;
    wstr res_buf;
};


/* modify attention. Worker, Protocol, Applicantion interface */
void initWorkerProcess(struct workerProcess *worker, char *worker_name);
void freeWorkerProcess(void *worker);
void workerProcessCron();
struct client *initClient(int fd, char *ip, int port, struct protocol *p, struct app *app);
void freeClient(struct client *);

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
 * */
struct protocol *spotProtocol(char *ip, int port, int fd);
struct app *spotAppInterface();


/* Sync worker Area */
void setupSync();
void syncWorkerCron();
int syncSendData(struct client *c);
int syncRecvData(struct client *c);

/* Async worker Area */
void setupAsync();
void asyncWorkerCron();
int asyncSendData(struct client *c);
int asyncRecvData(struct client *c);

#endif
