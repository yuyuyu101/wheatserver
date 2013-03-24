#include "redis.h"
#include "../../protocol/redis/proto_redis.h"

#define WHEAT_REDIS_UNIT_MIN     50
#define WHEAT_REDIS_TIMEOUT      1000000

struct redisUnit {
    size_t needed;
    size_t sended;
    size_t pos;
    struct conn *outer_conn;
    struct conn **redis_conns;
    struct redisInstance **sended_instances;
    struct token *first_token;
    struct listNode *node;
    struct timeval start;
    int retry;
};

static struct redisServer *RedisServer = NULL;
static struct protocol *RedisProtocol = NULL;
static void redisClientClosed(struct client *redis_client, void *data);

static struct redisInstance *getInstance(struct redisServer *server, int idx)
{
    struct redisInstance *instance = &server->instances[idx];
    if (instance->live && !instance->ntimeout)
        return instance;
    return NULL;
}

static int initInstance(struct redisInstance *instance)
{
    instance->redis_client = buildConn(instance->ip, instance->port, RedisProtocol);
    if (!instance->redis_client)
        return WHEAT_WRONG;
    instance->redis_client->client_data = instance;
    instance->live = 1;
    instance->ntimeout = 0;
    setClientFreeNotify(instance->redis_client, redisClientClosed, instance);
    registerClientRead(instance->redis_client);
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
    unit->needed = 0;
    unit->retry = 0;
    unit->pos = 0;
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
    finishConn(unit->outer_conn);
    removeListNode(RedisServer->message_center, unit->node);
    wfree(unit);
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
    iter = listGetIterator(instance->wait_units, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        unit = listNodeValue(node);
        unit->sended--;
        removeListNode(instance->wait_units, node);
    }
    freeListIterator(iter);
    instance->ntimeout = 0;
    wheatLog(WHEAT_WARNING, "one redis server disconnect: %s:%d, lived: %d",
            instance->ip, instance->port, RedisServer->live_instances);
}

static int sendOuterError(struct redisUnit *unit)
{
    struct slice error;
    char buf[255];
    int ret;
    ret = snprintf(buf, 255, "-ERR Server keep this key all timeout\r\n");
    sliceTo(&error, buf, ret);
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
        if (ret == -1)
            return WHEAT_WRONG;
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
    int ret;
    struct redisUnit *unit;

    if (isOuterClient(c->client)) {
        int nwritted = 0;
        ret = getRedisKey(c, &key);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
        token = hashDispatch(RedisServer, &key);
        unit = getRedisUnit();
        unit->outer_conn = c;
        if (isReadCommand(c))
            unit->needed = 1;
        else
            unit->needed = RedisServer->nbackup;

        while (nwritted < unit->needed) {
            if (!unit->first_token)
                unit->first_token = token;
            nwritted++;
            instance = getInstance(RedisServer, token->server_idx);
            token = token->next_server;
            if (!instance)
                continue;

            if (sendRedisData(c, instance, unit) == WHEAT_WRONG)
                return WHEAT_WRONG;
        }
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
            if (unit->pos == unit->needed) {
                sendOuterData(unit);
            }
        } else {
            ASSERT(instance->ntimeout > 0);
            instance->ntimeout--;
            finishConn(c);
        }

        removeListNode(instance->wait_units, node);
        return WHEAT_OK;
    }
}

void redisAppDeinit()
{
    int pos;
    for (pos = 0; pos < RedisServer->instance_size; pos++) {
        freeClient(RedisServer->instances[pos].redis_client);
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
    int pos = 0;
    long len, mem_len;
    uint8_t *p = NULL;
    struct listIterator *iter = NULL;
    struct redisInstance *instance = NULL;
    struct redisServer *server = RedisServer;
    conf = getConfiguration("redis-servers");
    if (!conf->target.ptr)
        return WHEAT_WRONG;

    len = listLength((struct list *)(conf->target.ptr));
    mem_len = sizeof(struct redisServer)+sizeof(struct redisInstance)*len;
    p = wmalloc(mem_len);
    memset(p, 0, mem_len);
    server = (struct redisServer*)p;

    server->instances = (struct redisInstance*)(p + sizeof(struct redisServer));
    struct listNode *node = NULL;
    iter = listGetIterator(conf->target.ptr, START_HEAD);
    int count = 0;
    while ((node = listNext(iter)) != NULL) {
        wstr *frags = wstrNewSplit(listNodeValue(node), ":", 1, &count);

        if (!frags) goto cleanup;
        if (count != 2) goto cleanup;
        instance = &server->instances[pos];
        instance->id = pos;
        instance->ip = wstrDup(frags[0]);
        instance->port = atoi(frags[1]);
        if (initInstance(instance) == WHEAT_WRONG)
            goto cleanup;
        instance->wait_units = createList();
        server->instance_size++;
        pos++;

        wstrFreeSplit(frags, count);
    }
    freeListIterator(iter);
    server->live_instances = server->instance_size;

    if (!server->instance_size ||
            server->instance_size < server->nbackup)
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
    return hashUpdate(server);

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
    if (isReadCommand(unit->outer_conn)) {
        // Read command means we only send request to *one* redis server.
        // Now we should choose next server which keep this key to retry
        // this unit request.
        instance = getInstance(RedisServer, unit->first_token->server_idx);
        wheatLog(WHEAT_NOTICE, "Instance %s:%d timeout response, try another",
                instance->ip, instance->port);
        instance->ntimeout++;
        next_token = unit->first_token->next_server;
        while (unit->retry < server->nbackup - 1) {
            unit->retry++;
            instance = getInstance(RedisServer, next_token->server_idx);
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
    if (server->instance_size != server->live_instances) {
        for (i = 0; i < server->instance_size; i++) {
            instance = &server->instances[i];
            if (!instance->live) {
                if (initInstance(instance) == WHEAT_WRONG)
                    continue;
                wheatLog(WHEAT_WARNING, "missed redis server connectd: %s:%d, lived: %d",
                        instance->ip, instance->port, RedisServer->live_instances);
            }
        }
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
