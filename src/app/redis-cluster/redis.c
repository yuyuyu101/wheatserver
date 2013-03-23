#include "redis.h"
#include "../../protocol/redis/proto_redis.h"

#define WHEAT_REDIS_CONNS_SIZE   5
#define WHEAT_MAX_REDIS_CONNS_SIZE   50

struct redisConnUnit {
    struct conn *outer_conn;
    struct client *redis_client;
    struct redisInstance *instance;
};

struct redisAppData {
    size_t count;
};

static struct redisServer *RedisServer = NULL;
static struct protocol *RedisProtocol = NULL;

static struct redisConnUnit *_newRedisUnit(struct redisInstance *instance)
{
    struct redisConnUnit *unit = wmalloc(sizeof(struct redisConnUnit));
    if (!unit) {
        return NULL;
    }
    unit->redis_client = buildConn(instance->ip, instance->port, RedisProtocol);
    if (!unit->redis_client) {
        wfree(unit);
        return NULL;
    }
    unit->outer_conn = NULL;
    unit->instance = instance;
    unit->redis_client->client_data = unit;
    return unit;
}

static struct redisConnUnit *getRedisUnit(struct redisInstance *instance, struct conn *outer_conn)
{
    struct redisConnUnit *unit = NULL;

    if (listLength(instance->free_conns) == 0) {
        unit = _newRedisUnit(instance);
    } else {
        struct listNode *node = listFirst(instance->free_conns);
        unit = listNodeValue(node);
        removeListNode(instance->free_conns, node);
    }
    ASSERT(isOuterClient(outer_conn->client));
    unit->outer_conn = outer_conn;
    return unit;
}

static void redisConnFree(struct redisConnUnit *redis_unit)
{
    redis_unit->outer_conn = NULL;
    unregisterClientRead(redis_unit->redis_client);
    appendToListTail(redis_unit->instance->free_conns, redis_unit);
}

static struct redisInstance *getInstance(struct redisServer *server, int idx)
{
    return &server->instances[idx];
}

int redisCall(struct conn *c, void *arg)
{
    struct redisInstance *instance = NULL;
    struct slice *next = NULL;
    struct redisConnUnit *redis_unit = NULL;
    struct slice key;
    struct token *token = NULL;
    struct redisAppData *app_data = c->app_private_data;
    struct conn *send_conn;
    int ret;

    if (isOuterClient(c->client)) {
        int nwritted = 0;
        ret = getRedisKey(c, &key);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
        token = hashDispatch(RedisServer, &key);
        while (nwritted < RedisServer->nwriter) {
            instance = getInstance(RedisServer, token->server_idx);
            redis_unit = getRedisUnit(instance, c);
            token = token->next_server;

            redisBodyStart(c);
            send_conn = connGet(redis_unit->redis_client);
            while ((next = redisBodyNext(c)) != NULL) {
                ret = sendClientData(send_conn, next);
                if (ret == -1)
                    return WHEAT_WRONG;
            }
            finishConn(send_conn);
            registerClientRead(redis_unit->redis_client);
            nwritted++;
        }
        return WHEAT_OK;
    } else {
        struct client *redis_client = c->client;
        redis_unit = redis_client->client_data;
        app_data = redis_unit->outer_conn->app_private_data;
        registerConnFree(redis_unit->outer_conn, (void (*)(void*))redisConnFree, redis_unit);
        registerConnFree(redis_unit->outer_conn, (void (*)(void*))finishConn, c);
        if (!--app_data->count) {
            while ((next = redisBodyNext(c)) != NULL) {
                ret = sendClientData(redis_unit->outer_conn, next);
                if (ret == -1)
                    return WHEAT_WRONG;
            }
            finishConn(redis_unit->outer_conn);
        }

        return WHEAT_OK;
    }
}

void *redisAppDataInit(struct conn *c)
{
    struct redisAppData *data = wmalloc(sizeof(*data));
    data->count = RedisServer->nwriter;
    return data;
}

void redisAppDataDeinit(void *data)
{
    struct redisAppData *d = data;
    wfree(d);
}

static int redisInstanceCreateConns(struct redisInstance *instance)
{
    int i = 0;
    struct redisConnUnit *conn;
    instance->server_conns = arrayCreate(sizeof(void*), WHEAT_REDIS_CONNS_SIZE);
    instance->free_conns = createList();
    for (; i < WHEAT_REDIS_CONNS_SIZE; ++i) {
        conn = _newRedisUnit(instance);
        if (!conn)
            return WHEAT_WRONG;
        arrayPush(instance->server_conns, conn);
        appendToListTail(instance->free_conns, conn);
    }
    return WHEAT_OK;
}

void redisAppDeinit()
{
    int pos;
    for (pos = 0; pos < RedisServer->instance_size; pos++) {
        arrayEach(RedisServer->instances[pos].server_conns, (void (*)(void *))freeClient);
    }
    if (RedisServer->tokens)
        wfree(RedisServer->tokens);
    wfree(RedisServer);
}

int redisAppInit(struct protocol *ptocol)
{
    ASSERT(ptocol);
    RedisProtocol = ptocol;
    struct configuration *conf = getConfiguration("redis-servers");
    int pos = 0, len, mem_len;
    uint8_t *p = NULL;
    struct listIterator *iter = NULL;
    if (!conf->target.ptr)
        return WHEAT_WRONG;

    len = listLength((struct list *)(conf->target.ptr));
    mem_len = sizeof(struct redisServer)+sizeof(struct redisInstance)*len;
    p = wmalloc(mem_len);
    memset(p, 0, mem_len);
    RedisServer = (struct redisServer*)p;

    RedisServer->instances = (struct redisInstance*)(p + sizeof(struct redisServer));
    struct listNode *node = NULL;
    iter = listGetIterator(conf->target.ptr, START_HEAD);
    int count = 0;
    while ((node = listNext(iter)) != NULL) {
        wstr *frags = wstrNewSplit(listNodeValue(node), ":", 1, &count);

        if (!frags) goto cleanup;
        if (count != 2) goto cleanup;
        RedisServer->instances[pos].id = pos;
        RedisServer->instances[pos].ip = wstrDup(frags[0]);
        RedisServer->instances[pos].port = atoi(frags[1]);
        if (redisInstanceCreateConns(&RedisServer->instances[pos]) == WHEAT_WRONG)
            goto cleanup;
        wstrFreeSplit(frags, count);
        pos++;
        RedisServer->instance_size++;
    }
    freeListIterator(iter);

    if (!RedisServer->instance_size ||
            RedisServer->instance_size < RedisServer->nbackup)
        goto cleanup;

    RedisServer->nreader = RedisServer->nwriter = RedisServer->nbackup = 2;
    return hashUpdate(RedisServer);

cleanup:
    wheatLog(WHEAT_WARNING, "redis init failed");
    if (iter) freeListIterator(iter);
    redisAppDeinit();
    return WHEAT_WRONG;
}
