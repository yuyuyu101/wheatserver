// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSEVER_WHEATSSERVER_H
#define WHEATSEVER_WHEATSSERVER_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dict.h"
#include "list.h"
#include "wstr.h"
#include "slice.h"
#include "array.h"

#include "debug.h"
#include "event.h"
#include "hook.h"
#include "memalloc.h"
#include "net.h"
#include "networking.h"
#include "portable.h"
#include "sig.h"
#include "stats.h"
#include "util.h"
#include "version.h"
#include "worker/worker.h"
#include "protocol/protocol.h"
#include "app/application.h"

/* Server Configuration */
#define WHEAT_DEFAULT_ADDR              "127.0.0.1"
#define WHEAT_SERVERPORT                10828
#define WHEATSERVER_CONFIGLINE_MAX      1024
#define WHEATSERVER_MAX_LOG_LEN         1024
#define WHEATSERVER_MAX_NAMELEN         1024
#define WHEATSERVER_PATH_LEN            1024
#define WHEATSERVER_GRACEFUL_TIME       5
#define WHEATSERVER_IDLE_TIME           1
#define WHEATSERVER_TIMEOUT             30
#define WHEAT_NOTFREE                   1
#define WHEATSERVER_CRON_MILLLISECONDS  100
#define WHEAT_PREALLOC_CLIENT_LIMIT     10000
#define WHEAT_BUFLIMIT                  (1024*1024*1024)
#define WHEAT_ARGS_NO_LIMIT             -1
#define WHEAT_MBUF_SIZE                 (16*1024)
#define WHEAT_PROTOCOL_DEFAULT          "Http"
#define WHEAT_CRON_HZ                   10

/* Statistic Configuration */
#define WHEAT_STATS_PORT       10829
#define WHEAT_STATS_ADDR       "127.0.0.1"
#define WHEAT_STAT_REFRESH     10
#define WHEAT_STAT_PACKET_MAX  512
#define WHEAT_DEFAULT_WORKER   "SyncWorker"
#define WHEAT_ASTERISK         "*"
#define WHEAT_PREALLOC_CLIENT  100
#define WHEAT_MAX_BUFFER_SIZE  4*1024*1024
#define WHEAT_MAX_FILE_LIMIT   16*1024*1024
#define WHEAT_STR_NULL         "NULL"

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


/* Using the following macro you can run code inside workerProcessCron() with
 * the specified period, specified in milliseconds.
 * The actual resolution depends on WHEAT_WOKER_HZ. */
// Learn from redis.c
#define runWithPeriod(_ms_) \
    if ((_ms_ <= 1000/WHEAT_CRON_HZ) || !(Server.cron_loops%((_ms_)/(1000/WHEAT_CRON_HZ))))
#define getMicroseconds(time) (time.tv_sec*1000000+time.tv_usec)


/* This exists some drawbacks globalServer include server configuration
 * and master info */
struct globalServer {
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
    char *stat_file;

    /* status */
    char master_name[WHEATSERVER_MAX_NAMELEN];
    int ipfd;
    struct evcenter *master_center;
    int stat_fd;
    struct timeval cron_time;
    long long cron_loops;
    pid_t pid;
    pid_t relaunch_pid;
    int pipe_readfd;
    int pipe_writefd;
    size_t mbuf_size;
    struct list *workers;
    struct list *master_clients;
    struct list *signal_queue;

    struct list *modules;
    struct list *confs;
    struct array *commands;
    struct array *stats;

    /* log */
    char *logfile;                               //logfile, stdout
    int verbose;                                 //logfile-level, NOTICE

    /* err buf */
    char *exit_reason;
    char neterr[NET_ERR_LEN];
};

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
    BOOL_FORMAT,
    LIST_FORMAT
};

struct configuration {
    char *name;
    int args;               // -1 means no limit on args
    int (*validator)(struct configuration *conf,  const char *key, const char *value);
    union {
        int val;
        void *ptr;
        struct enumIdName *enum_ptr;
    } target;
    void *helper;   // using in validator, indicating target attribute
    enum printFormat format;
};

struct command {
    char *command_name;
    int args;
    void (*command_func)(struct masterClient *);
    char *description;
};

struct moduleAttr {
    char *name;
    struct statItem *stats;
    size_t stat_size;
    struct configuration *confs;
    size_t conf_size;
    struct command *commands;
    size_t command_size;
};

struct workerProcess;

extern struct globalServer Server;

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
void spawnFakeWorker(void (*func)(void *), void *data);
void wakeUp();
/* graceful means whether to wait worker
 * conncction completion */
void stopWorkers(int graceful);
void halt(int exitcode);

/* configuration */
void loadConfigFile(const char *filename, char *options, int test);
void fillServerConfig();
void printServerConfig();
struct configuration *getConfiguration(const char *name);
void configCommand(struct masterClient *);

/* log */
void wheatLogRaw(int level, const char *msg);
void wheatLog(int level, const char *fmt, ...);

struct masterClient *createMasterClient(int fd);
void freeMasterClient(struct masterClient *c);
void logRedirect();

/* Configuration */
void initServerConfs(struct list *confs);
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);
int listValidator(struct configuration *conf, const char *key, const char *val);


#define WHEAT_WRONG -1
#define WHEAT_OK 0

#endif
