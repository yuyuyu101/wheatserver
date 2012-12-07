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
};

extern struct protocol protocolTable[];

struct worker {
    char *name;
    void (*setup)();
    void (*cron)();
};

extern struct worker workerTable[];

struct app {
    char *name;
    int (*constructor)(struct client *);
    void (*app)(struct client *);
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

/* worker's job:
 * 0. setup filling workerProcess members
 * 1. accept connection and init client
 * 2. recognize protocol(http or pop3 etc...)
 * 3. read big bulk data from socket
 * 4. call protocol parser
 * 5. call app constructor and pass the protocol parsed data
 * 6. call app callback and get the response
 * 7. construct response and send to
 * */
struct protocol *spotProtocol(char *ip, int port, int fd);
struct app *spotAppInterface();

#endif
