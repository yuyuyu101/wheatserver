// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "redis.h"
#include "../../protocol/redis/proto_redis.h"

#define WHEAT_REDIS_UNIT_MIN      50
#define WHEAT_REDIS_TIMEOUT       1000000
#define WHEAT_REDIS_ERR           "-ERR Server keep this key all broken\r\n"
#define WHEAT_REDIS_TIMEOUT_DIRTY 5


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
    int is_sended;
};

static struct redisServer *RedisServer = NULL;
static struct protocol *RedisProtocol = NULL;
static void redisClientClosed(struct client *redis_client, void *data);

static struct redisInstance *getInstance(struct redisServer *server, int idx,
        int is_readcommand)
{
    struct redisInstance *instance = arrayIndex(server->instances, idx);
    if (is_readcommand) {
        if (instance->live && !instance->is_dirty)
            return instance;
    } else {
        if (instance->live)
            return instance;
    }
    return NULL;
}

static int wakeupInstance(struct redisInstance *instance)
{
    instance->redis_client = buildConn(instance->ip, instance->port, RedisProtocol);
    if (!instance->redis_client)
        return WHEAT_WRONG;
    instance->redis_client->client_data = instance;
    instance->live = 1;
    instance->ntimeout = 0;
    instance->timeout_duration = 0;
    setClientFreeNotify(instance->redis_client, redisClientClosed, instance);
    registerClientRead(instance->redis_client);
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
    if (wakeupInstance(instance) == WHEAT_WRONG)
        return WHEAT_WRONG;
    instance->wait_units = createList();
    return WHEAT_OK;
}

static struct redisUnit *getRedisUnit()
{
    uint8_t *p;
    size_t count = (RedisServer->nbackup) * sizeof(void*) * 2;
    struct redisUnit *unit;
    p = wmalloc(sizeof(*unit)+count);
    unit = (struct redisUnit*)p;
    unit->sended = 0;
    unit->retry = 0;
    unit->pos = 0;
    unit->is_sended = 0;
    unit->outer_conn = NULL;
    unit->first_token = NULL;
    p += sizeof(*unit);
    unit->redis_conns = (struct conn**)p;
    p += (sizeof(void*) * RedisServer->nbackup);
    unit->sended_instances = (struct redisInstance **)p;
    unit->node = appendToListTail(RedisServer->message_center, unit);
    unit->start = Server.cron_time;
    return unit;
}

static void redisUnitFinal(struct redisUnit *unit)
{
    unit->is_sended = 1;
    if (unit->pos == unit->sended) {
        finishConn(unit->outer_conn);
        removeListNode(RedisServer->message_center, unit->node);
        wfree(unit);
    }
}

static void redisClientClosed(struct client *redis_client, void *data)
{
    struct redisInstance *instance = data;
    struct listIterator *iter;
    struct listNode *node;
    struct redisUnit *unit;
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
    struct slice *next;
    int ret;
    redisBodyStart(outer_conn);
    send_conn = connGet(instance->redis_client);
    while ((next = redisBodyNext(outer_conn)) != NULL) {
        ret = sendClientData(send_conn, next);
        if (ret == -1) {
            return WHEAT_WRONG;
        }
    }
    finishConn(send_conn);
    appendToListTail(instance->wait_units, unit);
    unit->sended_instances[unit->sended] = instance;
    unit->sended++;
    return WHEAT_OK;
}

// Suppose send first received response content
static int sendOuterData(struct redisUnit *unit)
{
    struct slice *next;
    int ret;
    struct conn *outer_conn = unit->outer_conn, *redis_conn;
    ASSERT(unit->pos > 0);
    redis_conn = unit->redis_conns[0];
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

int redisCall(struct conn *c, void *arg)
{
    struct redisInstance *instance = NULL;
    struct slice key;
    struct token *token = NULL;
    struct redisUnit *unit;

    if (isOuterClient(c->client)) {
        int nwritted = 0;
        size_t needed;
        struct redisServer *server = RedisServer;
        getRedisKey(c, &key);
        token = hashDispatch(server, &key);
        unit = getRedisUnit();
        unit->outer_conn = c;
        if (isReadCommand(c))
            needed = 1;
        else
            needed = server->nbackup;

        while (needed > 0 && nwritted < server->nbackup) {
            if (!unit->first_token)
                unit->first_token = token;
            nwritted++;
            instance = getInstance(server, token->server_idx,
                    isReadCommand(c));
            token = token->next_server;
            if (!instance)
                continue;

            if (sendRedisData(c, instance, unit) == WHEAT_WRONG)
                return WHEAT_WRONG;
            needed--;
        }
        // In case of all redis instance keep this key is down and we have to
        // send error message to client and only apply on read command.
        if (needed > 0 && isReadCommand(c))
            sendOuterError(unit);
        return WHEAT_OK;
    } else {
        struct listNode *node;
        struct client *redis_client = c->client;
        instance = redis_client->client_data;
        node = listFirst(instance->wait_units);
        ASSERT(node && listNodeValue(node));
        if (instance->ntimeout == 0) {
            unit = listNodeValue(node);
            registerConnFree(unit->outer_conn, (void (*)(void*))finishConn, c);
            unit->redis_conns[unit->pos++] = c;
            if (unit->pos == unit->sended) {
                sendOuterData(unit);
            }
        } else {
            ASSERT(instance->ntimeout > 0);
            instance->ntimeout--;
            if (instance->ntimeout == 0)
                instance->timeout_duration = 0;
            finishConn(c);
        }

        removeListNode(instance->wait_units, node);
        return WHEAT_OK;
    }
}

void redisAppDeinit()
{
    int pos;
    struct redisInstance *instance;
    for (pos = 0; pos < narray(RedisServer->instances); pos++) {
        instance = arrayIndex(RedisServer->instances, pos);
        freeClient(instance->redis_client);
    }
    if (RedisServer->tokens)
        wfree(RedisServer->tokens);
    wfree(RedisServer);
}

int redisAppInit(struct protocol *ptocol)
{
    ASSERT(ptocol);
    RedisProtocol = ptocol;
    struct configuration *conf;
    int pos, count;
    long len, mem_len;
    uint8_t *p = NULL;
    struct listIterator *iter = NULL;
    struct redisInstance ins, *instance;
    struct redisServer *server;
    conf = getConfiguration("redis-servers");
    if (!conf->target.ptr)
        return WHEAT_WRONG;

    len = listLength((struct list *)(conf->target.ptr));
    mem_len = sizeof(struct redisServer);
    p = wmalloc(mem_len);
    server = (struct redisServer*)p;

    server->instances = arrayCreate(sizeof(struct redisInstance), len);
    struct listNode *node = NULL;
    iter = listGetIterator(conf->target.ptr, START_HEAD);
    count = 0;
    pos = 0;
    while ((node = listNext(iter)) != NULL) {
        wstr *frags = wstrNewSplit(listNodeValue(node), ":", 1, &count);

        if (!frags) goto cleanup;
        if (count != 2) goto cleanup;
        arrayPush(server->instances, &ins);
        instance = arrayIndex(server->instances, pos);
        if (initInstance(instance, pos, frags[0], atoi(frags[1]), NONDIRTY) ==
                WHEAT_WRONG)
            goto cleanup;
        server->max_id = pos;
        pos++;
        wstrFreeSplit(frags, count);
    }
    freeListIterator(iter);
    server->live_instances = narray(server->instances);

    if (!server->live_instances ||
            server->live_instances < server->nbackup)
        goto cleanup;

    conf = getConfiguration("backup-size");
    if (!conf)
        return WHEAT_WRONG;
    server->nbackup = conf->target.val;

    conf = getConfiguration("redis-timeout");
    if (!conf)
        return WHEAT_WRONG;
    // `redis-timeout` is millisecond, we want microsecond
    server->timeout = conf->target.val * 1000;

    server->message_center = createList();
    RedisServer = server;
    return hashInit(server);

cleanup:
    wheatLog(WHEAT_WARNING, "redis init failed");
    if (iter) freeListIterator(iter);
    RedisServer = server;
    redisAppDeinit();
    return WHEAT_WRONG;
}

// If this unit is timeout, the instance which should be responsibility to
// should be punish and plus instance->ntimeout.
// We suppose the next response will receive from this timeout instance must
// be this unit wanted. And in `redisCall` according to instance->ntimeout,
// we will kill this response
static void handleTimeout(struct redisUnit *unit)
{
    struct redisServer *server = RedisServer;
    struct redisInstance *instance;
    struct token *next_token;
    int ret;
    int isreadcommand = isReadCommand(unit->outer_conn);
    if (isreadcommand) {
        // Read command means we only send request to *one* redis server.
        // Now we should choose next server which keep this key to retry
        // this unit request.
        instance = getInstance(RedisServer, unit->first_token->server_idx,
                isreadcommand);
        wheatLog(WHEAT_NOTICE, "Instance %s:%d timeout response, try another",
                instance->ip, instance->port);
        instance->ntimeout++;
        if (!instance->timeout_duration)
            instance->timeout_duration = Server.cron_time.tv_sec;
        next_token = unit->first_token->next_server;
        while (unit->retry < server->nbackup - 1) {
            unit->retry++;
            instance = getInstance(RedisServer, next_token->server_idx,
                    isreadcommand);
            unit->first_token = next_token;
            if (instance) {
                // This node must be the oldest unit in `message_center`,
                // so if retry send that this unit should be rotate to last
                removeListNode(server->message_center, unit->node);
                ret = sendRedisData(unit->outer_conn, instance, unit);
                unit->node = appendToListTail(RedisServer->message_center, unit);
                if (ret == WHEAT_OK)
                    break;
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
        size_t i;
        for (i = unit->pos; i < unit->sended; i++) {
            instance = unit->sended_instances[i];
            instance->ntimeout++;
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
    struct redisServer *server = RedisServer;
    struct redisInstance *instance = NULL;
    size_t i;
    long length = listLength(server->message_center);
    long now_micro = Server.cron_time.tv_sec * 1000000 + Server.cron_time.tv_usec;
    long micro_seconds;
    for (i = 0; i < narray(server->instances); i++) {
        instance = arrayIndex(server->instances, i);
        if (!instance->live) {
            if (wakeupInstance(instance) == WHEAT_WRONG)
                continue;
            wheatLog(WHEAT_WARNING, "missed redis server connectd: %s:%d, lived: %d",
                    instance->ip, instance->port, RedisServer->live_instances);
        }
        if (instance->timeout_duration > WHEAT_REDIS_TIMEOUT_DIRTY)
            instance->is_dirty = 1;
    }
    i = WHEAT_REDIS_UNIT_MIN > length ? WHEAT_REDIS_UNIT_MIN : length;
    struct listNode *node;
    struct redisUnit *unit;
    struct listIterator *iter = listGetIterator(server->message_center, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        if (!--i)
            break;
        unit = listNodeValue(node);
        micro_seconds = unit->start.tv_sec * 1000000 + unit->start.tv_usec;
        if (now_micro - micro_seconds > server->timeout) {
            wheatLog(WHEAT_NOTICE, "wait redis response timeout");
            handleTimeout(unit);
        } else {
            break;
        }
    }
    freeListIterator(iter);
}
