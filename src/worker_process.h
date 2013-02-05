#ifndef _WORKER_H
#define _WORKER_H

#include <unistd.h>
#include "wstr.h"

#define WORKER_BOOT_ERROR 3
#define WHEATSERVER_REQ_MAXLEN 8*1024

struct workerProcess {
    pid_t pid;
    pid_t ppid;
    int pipe_readfd;
    int pipe_writefd;
    int alive;

    char *worker_name;
    struct worker *worker;

    // Worker Statistic
    time_t stat_start_time;             // Worker process start time
    long long stat_total_connection;    // Number of the total client
    long long stat_total_request;       // Number of the total request parsed
    // Number of the failed request parsed. Such as http protocol, non-200
    // response is included
    long long stat_failed_request;
    // Max size of request buffer size since worker started
    long long stat_buffer_size;
    long long stat_work_time;           // Total of handling request time
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
    int (*sendData)(int fd, wstr *buf);
    int (*recvData)(int fd, wstr *buf);
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

    wstr buf;
    wstr res_buf;
};


/* modify attention. Worker, Protocol, Applicantion interface */
void initWorkerProcess(char *worker_name);
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
 * 1. provide with send and receive api
 * 2. if parent changed, worker must detect and exit
 * 3. if alive == 0, worker must exit
 * */
struct protocol *spotProtocol(char *ip, int port, int fd);
struct app *spotAppInterface();


/* Sync worker Area */
void setupSync();
void syncWorkerCron();
int syncSendData(int, wstr *);
int syncRecvData(int, wstr *);

#endif
