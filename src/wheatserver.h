#ifndef WHEATSEVER_WHEATSSERVER_H
#define WHEATSEVER_WHEATSSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <stddef.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <time.h>

#include "version.h"
#include "event.h"
#include "stats.h"
#include "wstr.h"
#include "dict.h"
#include "net.h"
#include "hook.h"
#include "list.h"
#include "sig.h"
#include "util.h"
#include "worker_process.h"
#include "networking.h"

/* Server Configuration */
#define WHEAT_SERVERPORT              10828
#define WHEATSERVER_CONFIGLINE_MAX    1024
#define WHEATSERVER_MAX_LOG_LEN       1024
#define WHEATSERVER_MAX_NAMELEN       1024
#define WHEATSERVER_PATH_LEN          1024
#define WHEATSERVER_GRACEFUL_TIME     5
#define WHEATSERVER_IDLE_TIME         1
#define WHEATSERVER_TIMEOUT           30
#define WHEAT_NOTFREE                 1
#define WHEATSERVER_CRON              1
#define WHEAT_PREALLOC_CLIENT         100
#define WHEAT_PREALLOC_CLIENT_LIMIT   10000
#define WHEAT_MAX_BUFFER_SIZE         64*1024

/* Statistic Configuration */
#define WHEAT_STATS_PORT       10829
#define WHEAT_STATS_ADDR       "127.0.0.1"
#define WHEAT_STAT_REFRESH     10
#define WHEAT_STAT_PACKET_MAX  512
#define WHEAT_DEFAULT_WORKER   "SyncWorker"

/* Command Format */
#define WHEAT_START_SPLIT     "\r\r"

/* Log levels */
#define WHEAT_DEBUG       0
#define WHEAT_VERBOSE     1
#define WHEAT_NOTICE      2
#define WHEAT_WARNING     4
#define WHEAT_LOG_RAW     8

#define VALIDATE_OK       0
#define VALIDATE_WRONG    1

/* This exists some drawbacks taht globalServer include server configuration
 * and master info */
struct globalServer {
    /* if it can be configated, right comment is the config name and default value */
    /* base Configuration */
    char *bind_addr;                             //bind-addr, *
    int port;                                    //port, 10828
    int worker_number;                           //worker-number, 2
    char *worker_type;                           //worker-type, SyncWorker
    char configfile_path[WHEATSERVER_PATH_LEN];
    struct hookCenter *hook_center;
    int graceful_timeout;
    int idle_timeout;
    int daemon;                                  //daemon, off
    char *pidfile;                               //pidfile, NULL
    int max_buffer_size;                         //max-buffer-size, 0
    int worker_timeout;                          //timeout-seconds, 30

    char *stat_addr;                             //stat-bind-addr, 127.0.0.1
    int stat_port;                               //stat-port, 10829
    int stat_refresh_seconds;                    //stat-refresh-time, 10

    /* status */
    int ipfd;
    struct evcenter *master_center;
    int stat_fd;
    struct workerStat *aggregate_workers_stat;
    time_t cron_time;
    struct masterStat *master_stat;
    pid_t pid;
    pid_t relaunch_pid;
    int pipe_readfd;
    int pipe_writefd;
    struct list *workers;
    struct list *master_clients;
    struct list *signal_queue;
    char master_name[WHEATSERVER_MAX_NAMELEN];

    /* log */
    char *logfile;                               //logfile, stdout
    int verbose;                                 //logfile-level, NOTICE

    /* err buf */
    char *exit_reason;
    char neterr[NET_ERR_LEN];
};

extern struct globalServer Server;

struct masterClient {
    int fd;
    wstr request_buf;
    wstr response_buf;
    int argc;
    wstr *argv;
};

struct enumIdName {
    int id;
    char *name;
};

enum printFormat {
    INT_FORMAT,
    STRING_FORMAT,
    ENUM_FORMAT,
    BOOL_FORMAT
};

struct configuration {
    char *name;
    int args;
    int (*validator)(struct configuration *conf,  const char *key, const char *value);
    union {
        int val;
        char *ptr;
        struct enumIdName *enum_ptr;
    } target;
    void *helper;   // using in validator, indicating target attribute
    enum printFormat format;
};

void initServer();

/* restart */
void reload();
void reexec();

/* worker manage */
void adjustWorkerNumber();
void murderIdleWorkers();
void killWorker(struct workerProcess *worker, int sig);
void killAllWorkers(int sig);
void spawnWorker(char *worker_name);
void wakeUp();
/* graceful means whether to wait worker
 * conncction completion */
void stopWorkers(int graceful);
void halt(int exitcode);

/* configuration */
void loadConfigFile(const char *filename, char *options);
void fillServerConfig();
void printServerConfig();
struct configuration *getConfiguration(const char *name);
void configCommand(struct masterClient *);

/* log */
void wheatLogRaw(int level, const char *msg);
void wheatLog(int level, const char *fmt, ...);

struct masterClient *createMasterClient(int fd);
void freeMasterClient(struct masterClient *c);

#define WHEAT_WRONG -1
#define WHEAT_OK 0

#endif
