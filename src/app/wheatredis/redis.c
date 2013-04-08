// WheatRedis module
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "redis.h"

#define WHEAT_REDIS_UNIT_MIN        50
#define WHEAT_REDIS_TIMEOUT         1000000
#define WHEAT_REDIS_ERR             "-ERR Server keep this key all broken\r\n"
#define WHEAT_REDIS_TIMEOUT_DIRTY   5
#define WHEAT_REDIS_REQ_LEN         64

#define WHEAT_REDIS_USEFILE         0
#define WHEAT_REDIS_USEREDIS        1
#define WHEAT_REDIS_REDISTHENFILE   2

int redisCall(struct conn *c, void *arg);
int redisAppInit(struct protocol *);
void redisAppDeinit();
void redisAppCron();
static void *redisAppDataInit(struct conn *c);
static void redisAppDataDeinit(void *data);
static void redisAddServerCommand(struct masterClient *c);

static struct enumIdName RedisSources[] = {
    {0, "UseFile"}, {1, "UseRedis"}, {2, "RedisThenFile"},
};

static struct configuration RedisConf[] = {
    {"redis-servers",     WHEAT_ARGS_NO_LIMIT,listValidator, {.ptr=NULL},
        NULL,                   LIST_FORMAT},
    {"backup-size",       2, unsignedIntValidator, {.val=1},
        NULL,                   INT_FORMAT},
    {"redis-timeout",     2, unsignedIntValidator, {.val=1000},
        NULL,                   INT_FORMAT},
    {"config-server",     2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"config-source",     2, enumValidator,        {.enum_ptr=&RedisSources[2]},
        &RedisSources[0],       ENUM_FORMAT},
};

static struct statItem RedisStats[] = {
    {"Current redis unit count", ASSIGN_STAT, RAW, 0, 0},
    {"Total redis unit count", SUM_STAT, RAW, 0, 0},
    {"Total timeout response", SUM_STAT, RAW, 0, 0},
};

static struct command RedisCommand[] = {
    {"redis-addnode", 3, redisAddServerCommand, "redis-addnode ip port"},
};

static struct moduleAttr AppRedisAttr = {
    "WheatRedis", RedisStats, sizeof(RedisStats)/sizeof(struct statItem),
    RedisConf, sizeof(RedisConf)/sizeof(struct configuration),
    RedisCommand, sizeof(RedisCommand)/sizeof(struct command)
};

static long long *CurrentUnitCount = NULL;
static long long *TotalUnitCount = NULL;
static long long *TotalTimeoutResponse = NULL;

struct redisAppData {
    struct redisUnit *unit;
    wstr header;
};

struct app AppRedis = {
    &AppRedisAttr, "Redis", redisAppCron, redisCall,
    redisAppInit, redisAppDeinit, redisAppDataInit, redisAppDataDeinit, 0
};

// TODO:
// 1. when a redis instance is marked as `is_dirty` and sync data from others

struct redisUnit {
    size_t sended;
    size_t pos;
    struct conn *outer_conn;
    struct conn **redis_conns;
    struct redisInstance **sended_instances;
    struct token *first_token;
    struct listNode *node;
    struct timeval start;
    int retry;
    unsigned is_read:1;
    unsigned wait_free:1;
};

static struct redisServer *RedisServer = NULL;
static struct protocol *RedisProtocol = NULL;
static void redisClientClosed(struct client *redis_client);
void redisAppDeinit();

static struct redisInstance *getInstance(struct redisServer *server, size_t idx,
        int is_readcommand)
{
    struct redisInstance *instance = arrayIndex(server->instances, idx);
    if (instance->live)
        return instance;
    return NULL;
}

static int wakeupInstance(struct redisInstance *instance)
{
    char name[255];

    instance->redis_client = buildConn(instance->ip, instance->port, RedisProtocol);
    if (!instance->redis_client)
        return WHEAT_WRONG;
    instance->redis_client->client_data = instance;
    instance->live = 1;
    instance->ntimeout = 0;
    instance->timeout_duration = 0;
    snprintf(name, 255, "Redis Instance %s:%d", instance->ip, instance->port);
    setClientName(instance->redis_client, name);
    setClientFreeNotify(instance->redis_client, redisClientClosed);
    return WHEAT_OK;
}

int initInstance(struct redisInstance *instance, size_t pos, wstr ip,
        int port, int is_dirty)
{
    instance->id = pos;
    instance->ip = wstrDup(ip);
    instance->port = port;
    instance->is_dirty = is_dirty;
    instance->ntoken = 0;
    instance->reliability = 0;
    instance->wait_units = createList();
    if (wakeupInstance(instance) == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "initInstance connect failed: %s:%d", ip, port);
        return WHEAT_WRONG;
    }
    return WHEAT_OK;
}

static struct redisUnit *getRedisUnit()
{
    uint8_t *p;
    size_t count;
    struct redisUnit *unit;

    count = (RedisServer->nbackup) * sizeof(void*) * 2;

    p = wmalloc(sizeof(*unit)+count);
    unit = (struct redisUnit*)p;
    unit->sended = 0;
    unit->retry = 0;
    unit->pos = 0;
    unit->outer_conn = NULL;
    unit->first_token = NULL;
    unit->is_read = 0;
    unit->wait_free = 0;
    p += sizeof(*unit);
    unit->redis_conns = (struct conn**)p;
    p += (sizeof(void*) * RedisServer->nbackup);
    unit->sended_instances = (struct redisInstance **)p;
    unit->node = appendToListTail(RedisServer->message_center, unit);
    unit->start = Server.cron_time;
    (*TotalUnitCount)++;
    return unit;
}

static void redisUnitFinal(struct redisUnit *unit)
{
    if (!unit->wait_free) {
        finishConn(unit->outer_conn);
        unit->outer_conn = NULL;
        unit->wait_free = 1;
    }
    if (unit->pos == unit->sended) {
        removeListNode(RedisServer->message_center, unit->node);
        wfree(unit);
    }
}

static void redisClientClosed(struct client *redis_client)
{
    struct redisInstance *instance;
    struct listIterator *iter;
    struct listNode *node;
    struct redisUnit *unit;

    instance = redis_client->client_data;
    RedisServer->live_instances--;
    instance->live = 0;
    instance->redis_client = NULL;
    instance->is_dirty = 1;
    iter = listGetIterator(instance->wait_units, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        unit = listNodeValue(node);
        unit->sended--;
        removeListNode(instance->wait_units, node);
    }
    freeListIterator(iter);
    wheatLog(WHEAT_WARNING, "one redis server disconnect: %s:%d, lived: %d",
            instance->ip, instance->port, RedisServer->live_instances);
}

static int sendOuterError(struct redisUnit *unit)
{
    struct slice error;
    char buf[255];
    int ret;
    ret = snprintf(buf, 255, WHEAT_REDIS_ERR);
    sliceTo(&error, (uint8_t *)buf, ret);
    ret = sendClientData(unit->outer_conn, &error);
    redisUnitFinal(unit);
    return ret;
}

static int sendRedisData(struct conn *outer_conn,
        struct redisInstance *instance, struct redisUnit *unit)
{
    struct conn *send_conn;
    struct slice *next, key, temp, command;
    int ret, args;
    size_t pos, key_end_pos, intercross, key_token_id;
    struct redisAppData *redis_data;

    send_conn = connGet(instance->redis_client);
    redis_data = outer_conn->app_private_data;
    getRedisKey(outer_conn, &key);
    getRedisCommand(outer_conn, &command);
    args = getRedisArgs(outer_conn);
    key_end_pos = getRedisKeyEndPos(outer_conn);
    pos = intercross = 0;
    key_token_id = unit->first_token->pos;

    redis_data->header = wstrNewLen(NULL, (int)key.len+(int)command.len+32);
    ret = snprintf(redis_data->header, wstrfree(redis_data->header),
            "*%d\r\n$%lu\r\n%s\r\n$%lu\r\n%lu%s", args,
            command.len, command.data,
            key.len+getIntLen(key_token_id), key_token_id, key.data);
    wstrupdatelen(redis_data->header, ret);

    sliceTo(&temp, (uint8_t*)redis_data->header, wstrlen(redis_data->header));
    if (sendClientData(send_conn, &temp) == -1)
        return WHEAT_WRONG;
    redisBodyStart(outer_conn);
    while ((next = redisBodyNext(outer_conn)) != NULL) {
        pos += next->len;
        if (pos < key_end_pos)
            continue;
        break;
    }
    if (!next) ASSERT(0);

    //   Example of `next` `key_end_pos` `intercross`
    //   *3\r\n$3\r\nget\r\n$3\r\nkey\r\n
    //     |            |           |
    //     |<---------->|           |
    //     | intercross |           |
    //     |            |           |
    //     |      key_end_pos       |
    //  next->data      |          pos
    //     |            |           |
    //     <------------------------>
    //              next->len

    intercross = next->len - (pos - key_end_pos);
    sliceTo(&temp, next->data+intercross, next->len - intercross);
    if (sendClientData(send_conn, &temp) == -1)
        return WHEAT_WRONG;
    while ((next = redisBodyNext(outer_conn)) != NULL) {
        if (sendClientData(send_conn, next) == -1)
            return WHEAT_WRONG;
    }
    finishConn(send_conn);
    appendToListTail(instance->wait_units, unit);
    unit->sended_instances[unit->sended] = instance;
    unit->sended++;
    return WHEAT_OK;
}

// send response to client
static int sendOuterData(struct redisUnit *unit)
{
    struct slice *next;
    int ret;
    struct conn *outer_conn, *redis_conn;
    ASSERT(unit->pos > 0);

    redis_conn = unit->redis_conns[0];
    outer_conn = unit->outer_conn;
    redisBodyStart(redis_conn);
    while ((next = redisBodyNext(redis_conn)) != NULL) {
        ret = sendClientData(outer_conn, next);
        if (ret == -1) {
            redisUnitFinal(unit);
            return WHEAT_WRONG;
        }
    }
    redisUnitFinal(unit);
    return WHEAT_OK;
}

// When client requests comes, we iterate backup instances which keep this key.
// If is read command, we choose the non-dirty instance. If non-dirty instance
// is None, we choose the max reliability instance. If is write command, send
// write command to all backup instances.
// If all instance isn't alive, send error to client(very rare)
static int handleClientRequests(struct conn *c)
{
    struct token *token;
    struct redisUnit *unit;
    struct slice key;
    int nwritted;
    struct redisInstance *instance, *reliability_instance, *nondirty_instance;
    struct redisServer *server;
    struct redisAppData *redis_data;

    server = RedisServer;
    getRedisKey(c, &key);
    token = hashDispatch(server, &key);
    unit = getRedisUnit();
    unit->outer_conn = c;
    redis_data = c->app_private_data;

    nwritted = 0;
    reliability_instance = nondirty_instance = NULL;
    unit->is_read = isReadCommand(c);
    while (nwritted < server->nbackup) {
        if (!unit->first_token)
            unit->first_token = token;
        nwritted++;
        instance = getInstance(server, token->instance_id,
                isReadCommand(c));
        token = &server->tokens[token->next_instance];
        if (!instance)
            continue;

        if (unit->is_read) {
            if (!reliability_instance)
                reliability_instance = instance;
            else if (reliability_instance->reliability < instance->reliability)
                reliability_instance = instance;

            if (!instance->is_dirty)
                nondirty_instance = instance;
        } else {
            sendRedisData(c, instance, unit);
        }
    }

    if (unit->is_read) {
        if (nondirty_instance) {
            instance = nondirty_instance;
        } else if (reliability_instance) {
            instance = reliability_instance;
        }
        if (instance)
            sendRedisData(c, instance, unit);
    }

    // Check last to ensure at least one request is sent to redis server,
    // otherwise send error to client.
    if (!unit->sended) {
        sendOuterError(unit);
    } else {
        redis_data->unit = unit;
    }
    return WHEAT_OK;
}

static int handleRedisResponse(struct conn *c)
{
    struct listNode *node;
    struct client *redis_client;
    struct redisInstance *instance;
    struct redisUnit *unit;

    redis_client = c->client;
    instance = redis_client->client_data;
    node = listFirst(instance->wait_units);
    ASSERT(node && listNodeValue(node));
    unit = listNodeValue(node);

    if (!unit->wait_free) {
        // Means response to client isn't sent
        registerConnFree(unit->outer_conn, (void (*)(void*))finishConn, c);
        unit->redis_conns[unit->pos++] = c;
        if (unit->is_read)
            sendOuterData(unit);
        else if (unit->pos == unit->sended)
            sendOuterData(unit);
    } else {
        // Means response to client have been sent, now only to finishConn
        finishConn(c);
    }
    if (instance->ntimeout)
        instance->ntimeout--;

    removeListNode(instance->wait_units, node);
    return WHEAT_OK;
}

int redisCall(struct conn *c, void *arg)
{
    int ret;
    struct redisServer *server;

    server = RedisServer;
    if (server->is_serve) {
        if (isOuterClient(c->client))
            ret = handleClientRequests(c);
        else
            ret = handleRedisResponse(c);
        return ret;
    } else {
        return handleConfig(server, c);
    }
}

void redisAppDeinit()
{
    int pos;
    struct redisInstance *instance;
    struct redisServer *server = RedisServer;

    for (pos = 0; pos < narray(server->instances); pos++) {
        instance = arrayIndex(server->instances, pos);
        freeClient(instance->redis_client);
        freeList(instance->wait_units);
    }

    listEach(server->pending_conns, (void (*)(void*))finishConn);
    listEach(server->message_center, wfree);
    freeList(server->message_center);
    arrayDealloc(server->instances);
    if (server->tokens)
        wfree(server->tokens);
    if (server->config_server)
        configServerDealloc(server->config_server);
    wfree(server);
}

struct client *connectConfigServer(char *option)
{
    int count, port;
    struct client *config_client;

    config_client = NULL;
    wstr ip, *frags,ws = wstrNew(option);
    frags = wstrNewSplit(ws, ":", 1, &count);
    wstrFree(ws);

    if (!frags || count != 2)
        goto failed;

    ip = frags[0];
    port = atoi(frags[1]);
    config_client = buildConn(ip, port, RedisProtocol);
failed:
    wstrFreeSplit(frags, count);
    return config_client;
}

static void removePendingConn(struct client *client)
{
    struct listNode *node;
    struct listIterator *iter;
    struct conn *conn;
    struct redisServer *server;

    server = RedisServer;
    iter = listGetIterator(RedisServer->pending_conns, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        conn = listNodeValue(node);
        if (conn->client == client) {
            // If `pending_conn->client` is `client`, just remove it
            removeListNode(server->pending_conns, node);
        }
    }
    freeListIterator(iter);
}

void appendToPendingConn(struct conn *c)
{
    // In case of remote client close connection  We need to be notify
    // and remove conn from `pending_conns` avoid incorrect access
    setClientFreeNotify(c->client, removePendingConn);
    appendToListTail(RedisServer->pending_conns, c);
}

static void redisAddServer(struct masterClient *c)
{
    int i;
    wstr ip;
    long long port;
    size_t len;
    struct redisInstance *instance, *instances;

    ip = c->argv[1];
    if (string2ll(c->argv[2], wstrlen(c->argv[2]), &port) == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "parse port failed %d", port);
        return ;
    }

    len = narray(RedisServer->instances);
    instances = arrayData(RedisServer->instances);
    for (i = 0; i < len; i++) {
        instance = &instances[i];
        if (!wstrCmp(instance->ip, ip) && port == instance->port) {
            wheatLog(WHEAT_WARNING,
                    "add redis server failed, %s:%d have already in WheatRedis",
                    ip, port);
            return;
        }
    }

    if (hashAdd(RedisServer, ip, port, ++RedisServer->max_id)) {
        wheatLog(WHEAT_WARNING, "add redis server failed %s:%d", ip, port);
        --RedisServer->max_id;
        return ;
    }
}

static void redisAddServerCommand(struct masterClient *c)
{
    //spawnFakeWorker((void (*)(void *))redisAddServer, c);
}

// We should deal with some conditions about config_source:
// - RedisThenFile
// 1. config-server is not specified or can't build connection: use file and
// not save to config-server
// 2. config-server is specified and build connection successfully, but get
// info from config-server failed or incompletely: use config file and save
// to config-server
// 3. config-server is specified and build connection successfully, get info
// from config-server is valid: use config-server and not save
//
// - UseRedis
// 1. config-server is not specified or can't build connection, get info from
// config-server is unvalid: directly exit and log error
// 2. config-server is specified and build connection successfully, get info
// from config-server is valid: use config-server and not save
//
// - UseFile
// 1. Use file directly and ignore config-server
int redisAppInit(struct protocol *ptocol)
{
    ASSERT(ptocol);
    RedisProtocol = ptocol;
    struct configuration *config_source, *conf;
    uint8_t *p;
    struct redisServer *server;
    struct client *config_client;
    int ret;
    int use_redis_only;

    CurrentUnitCount = &getStatValByName("Current redis unit count");
    TotalUnitCount = &getStatValByName("Total redis unit count");
    TotalTimeoutResponse = &getStatValByName("Total timeout response");

    p = wmalloc(sizeof(struct redisServer));
    RedisServer = server = (struct redisServer*)p;
    server->message_center = createList();
    server->pending_conns = createList();
    server->instances = arrayCreate(sizeof(struct redisInstance), 10);
    server->config_server = NULL;
    server->live_instances = 0;
    server->tokens = wmalloc(sizeof(struct token)*WHEAT_KEYSPACE);
    server->ntoken = WHEAT_KEYSPACE;
    server->is_serve = 0;

    config_source = getConfiguration("config-source");
    use_redis_only = config_source->target.enum_ptr->id == WHEAT_REDIS_USEREDIS;
    if (config_source->target.enum_ptr->id != WHEAT_REDIS_USEFILE) {
        conf = getConfiguration("config-server");
        if (conf->target.ptr) {
            config_client = connectConfigServer(conf->target.ptr);
            if (config_client) {
                server->config_server = configServerCreate(config_client,
                        config_source->target.enum_ptr->id == WHEAT_REDIS_USEREDIS);
                ret = getServerFromRedis(server);
                if (ret == WHEAT_WRONG)
                    return WHEAT_WRONG;
                return WHEAT_OK;
            }
        }
        wheatLog(WHEAT_WARNING,
                "No config server specified or can't build connection");
    }
    if (!use_redis_only) {
        if (configFromFile(server) == WHEAT_WRONG)
            return WHEAT_WRONG;
        server->is_serve = 1;
        return WHEAT_OK;
    }

    wheatLog(WHEAT_WARNING, "No path access here");
    return WHEAT_WRONG;
}

static void *redisAppDataInit(struct conn *c)
{
    struct redisAppData *data;

    data = wmalloc(sizeof(*data));
    data->unit = NULL;
    data->header = NULL;
    return data;
}

static void redisAppDataDeinit(void *data)
{
    struct redisAppData *redis_data;

    redis_data = data;
    if (redis_data->unit)
        redis_data->unit->wait_free = 1;
    if (redis_data->header)
        wstrFree(redis_data->header);
    wfree(redis_data);
}

// If this unit is timeout, the instance which should be responsibility to
// should be punish and plus instance->ntimeout.
// We suppose the next response will receive from this timeout instance must
// be this unit wanted. And in `redisCall` according to instance->ntimeout,
// we will kill this response
static void handleTimeout(struct redisUnit *unit)
{
    struct redisServer *server;
    struct redisInstance *instance;
    struct token *next_token;
    int ret;
    int isreadcommand;
    size_t i;

    server = RedisServer;
    isreadcommand = isReadCommand(unit->outer_conn);

    if (isreadcommand) {
        // Read command means we only send request to *one* redis server.
        // Now we should choose next server which keep this key to retry
        // this unit request.
        instance = getInstance(server, unit->first_token->instance_id,
                isreadcommand);
        wheatLog(WHEAT_NOTICE, "Instance %s:%d timeout response, try another",
                instance->ip, instance->port);
        instance->ntimeout++;
        instance->reliability--;
        if (!instance->timeout_duration)
            instance->timeout_duration = Server.cron_time.tv_sec;
        next_token = &server->tokens[unit->first_token->next_instance];
        while (unit->retry < server->nbackup - 1) {
            unit->retry++;
            instance = getInstance(server, next_token->instance_id,
                    isreadcommand);
            unit->first_token = next_token;
            if (instance) {
                // This node must be the oldest unit in `message_center`,
                // so if retry send that this unit should be rotate to last
                removeListNode(server->message_center, unit->node);
                ret = sendRedisData(unit->outer_conn, instance, unit);
                unit->node = appendToListTail(server->message_center, unit);
                if (ret == WHEAT_OK)
                    return ;
            }
        }
        if (unit->retry >= server->nbackup - 1) {
            wheatLog(WHEAT_NOTICE, "Read command failed, retry %d instance",
                    unit->retry);
            sendOuterError(unit);
        }
    } else {
        // Write command will send request to all redis server and if have
        // timeout response we should judge whether send response to client
        for (i = unit->pos; i < unit->sended; i++) {
            instance = unit->sended_instances[i];
            instance->ntimeout++;
            instance->reliability--;
            if (!instance->timeout_duration)
                instance->timeout_duration = Server.cron_time.tv_sec;
            wheatLog(WHEAT_NOTICE,
                    "Instance %s:%d write timeout response, it may result in inconsistence",
                    instance->ip, instance->port);
        }
        if (unit->pos > 0) {
            sendOuterData(unit);
        } else {
            wheatLog(WHEAT_NOTICE,
                    "Write command failed, none instance response data");
            sendOuterError(unit);
        }
    }
}

void redisAppCron()
{
    struct redisServer *server;
    struct redisInstance *instance;
    size_t i;
    struct listNode *node;
    struct redisUnit *unit;
    struct listIterator *iter;
    long length;
    long now_micro;
    long micro_seconds;

    server = RedisServer;
    length = listLength(server->message_center);
    now_micro = getMicroseconds(Server.cron_time);

    if (!server->is_serve) {
        // server is not starting, we can infer that user is choosing redis to
        // get config. And now we need to affirm is init config fininshed.
        // If init config from redis fininshed, we should clean up
        // `config_server`
        if (isStartServe(server)) {
            server->is_serve = 1;
            wheatLog(WHEAT_VERBOSE, "WheatRedis is starting");
            configServerDealloc(server->config_server);
            server->config_server = NULL;
        }
        return ;
    }

    for (i = 0; i < narray(server->instances); i++) {
        instance = arrayIndex(server->instances, i);
        // refresh client avoid being closed because timeout
        if (!instance->live) {
            if (wakeupInstance(instance) == WHEAT_WRONG)
                continue;
            server->live_instances++;
            wheatLog(WHEAT_WARNING, "missed redis server connectd: %s:%d, lived: %d",
                    instance->ip, instance->port, RedisServer->live_instances);
        } else {
            refreshClient(instance->redis_client);
        }
        if (!instance->ntimeout)
            instance->timeout_duration = 0;
        if (instance->timeout_duration > WHEAT_REDIS_TIMEOUT_DIRTY)
            instance->is_dirty = 1;
    }

    i = WHEAT_REDIS_UNIT_MIN > length ? WHEAT_REDIS_UNIT_MIN : length;
    iter = listGetIterator(server->message_center, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        if (!--i)
            break;
        unit = listNodeValue(node);
        if (unit->wait_free)
            continue;
        micro_seconds = unit->start.tv_sec * 1000000 + unit->start.tv_usec;
        if (now_micro - micro_seconds > server->timeout) {
            wheatLog(WHEAT_NOTICE, "wait redis response timeout");
            handleTimeout(unit);
            TotalTimeoutResponse++;
        } else {
            break;
        }
    }
    freeListIterator(iter);

    *CurrentUnitCount = listLength(server->message_center);

    if (listFirst(server->pending_conns)) {
        listEach2(server->pending_conns,
                (void (*)(void*, void*))redisCall, NULL);
        listClear(server->pending_conns);
    }
}
