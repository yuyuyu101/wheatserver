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

#include "array.h"
#include "dict.h"
#include "list.h"
#include "slice.h"
#include "wstr.h"

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

// Server Configuration
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
#define WHEAT_ARGS_NO_LIMIT             (-1)
#define WHEAT_MBUF_SIZE                 (16*1024)
#define WHEAT_PROTOCOL_DEFAULT          "Http"
#define WHEAT_CRON_HZ                   10

// Statistic Configuration
#define WHEAT_STATS_PORT       10829
#define WHEAT_STATS_ADDR       "127.0.0.1"
#define WHEAT_STAT_REFRESH     10
#define WHEAT_STAT_PACKET_MAX  512
#define WHEAT_DEFAULT_WORKER   "SyncWorker"
#define WHEAT_ASTERISK         "*"
#define WHEAT_PREALLOC_CLIENT  100
#define WHEAT_MAX_BUFFER_SIZE  (4*1024*1024)
#define WHEAT_MAX_FILE_LIMIT   (16*1024*1024)
#define WHEAT_STR_NULL         "NULL"

// Command Format
#define WHEAT_START_SPLIT     "\r\r"

// Log levels
#define WHEAT_DEBUG       0
#define WHEAT_VERBOSE     1
#define WHEAT_NOTICE      2
#define WHEAT_WARNING     4
#define WHEAT_LOG_RAW     8

// Config validator return values
#define VALIDATE_OK       0
#define VALIDATE_WRONG    1

/* Using the following macro you can run code inside workerProcessCron() with
 * the specified period, specified in milliseconds.
 * The actual resolution depends on WHEAT_WOKER_HZ. */
// Learn from redis.c
#define runWithPeriod(_ms_) \
    if ((_ms_ <= 1000/WHEAT_CRON_HZ) || !(Server.cron_loops%((_ms_)/(1000/WHEAT_CRON_HZ))))
#define getMicroseconds(time) (time.tv_sec*1000000+time.tv_usec)


// globalServer is only one for Wheatserver instance.
//
// `worker_type`: Worker module name
// `graceful_timeout`: interval for worker process exit gracefully
// `worker_time`: interval for worker process timeout trigger
// `pipe_readfd`, `pipe_writefd`: used to wake up master process, it's relevant
// for signal handler
// `workers`: alive worker process list
// `master_clients`: clients connect to master process(statistic listen)
// `signal_queue`: signal waiting queue
// `modules`: aggregation of modules including worker, protocol, application
// `confs`: the collection of configuration items in modules
// `commands`: the collection of command items in modules
// `stats`: the collection of statistic items in modules
struct globalServer {
    char *bind_addr;
    int port;
    int worker_number;
    char *worker_type;
    char configfile_path[WHEATSERVER_PATH_LEN];
    struct hookCenter *hook_center;
    int graceful_timeout;
    int daemon;
    char *pidfile;
    int max_buffer_size;
    int worker_timeout;

    char *stat_addr;
    int stat_port;
    int stat_refresh_seconds;
    char *stat_file;

    // status
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

    // log
    char *logfile;
    int verbose;

    // error
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

// `target` is the actual field to store config value. You can use int, pointer
// or enum type as your config type. Directly store right field.
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

enum moduleType {
    APP,
    PROTOCOL,
    WORKER,
};

struct moduleAttr {
    char *name;
    enum moduleType type;
    union {
        struct app *app;
        struct worker *worker;
        struct protocol *protocol;
        void *p;
    } module;
    struct statItem *stats;
    size_t stat_size;
    struct configuration *confs;
    size_t conf_size;
    struct command *commands;
    size_t command_size;
};

struct workerProcess;

extern struct globalServer Server;
extern struct moduleAttr *ModuleTable[];

void initServer();

// ============ Worker Process Restart =============
void reload();
void reexec();

// ============ Worker Process Management ==========
void adjustWorkerNumber();
void murderIdleWorkers();
void killWorker(struct workerProcess *worker, int sig);
void killAllWorkers(int sig);
void spawnWorker(char *worker_name);
void spawnFakeWorker(void (*func)(void *), void *data);
void wakeUp();
// graceful means whether to wait worker
// conncction completion
void stopWorkers(int graceful);
void halt(int exitcode);

// ============== Configuration ====================
void loadConfigFile(const char *filename, char *options, int test);
void fillServerConfig();
void printServerConfig();
struct configuration *getConfiguration(const char *name);
void configCommand(struct masterClient *);

// ============== Configuration validator ==========
void initServerConfs(struct list *confs);
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);
int listValidator(struct configuration *conf, const char *key, const char *val);

// =================== Log =========================
void wheatLogRaw(int level, const char *msg);
void wheatLog(int level, const char *fmt, ...);

// ============ Master Client Operation ============
struct masterClient *createMasterClient(int fd);
void freeMasterClient(struct masterClient *c);
void logRedirect();


#define WHEAT_WRONG -1
#define WHEAT_OK 0

// ============== Module Get Operation =============
#define getProtocol(m) (m->module.protocol)
#define getWorker(m) (m->module.worker)
#define getApp(m) (m->module.app)

struct moduleAttr *getModule(enum moduleType type, const char *name);
const char *getModuleName(enum moduleType type, void *t);
void getAppsByProtocol(struct array *apps, struct protocol *p);

static inline struct worker *spotWorker(const char *name)
{
    struct moduleAttr *_module_attr = getModule(WORKER, name);
    return _module_attr->module.worker;
}

static inline struct protocol *spotProtocol(const char *name)
{
    struct moduleAttr *_module_attr = getModule(PROTOCOL, name);
    return _module_attr->module.protocol;
}

static inline struct app *spotApp(const char *name)
{
    struct moduleAttr *_module_attr = getModule(APP, name);
    return _module_attr->module.app;
}

#endif
