#include "redis.h"
#include "../../protocol/redis/proto_redis.h"

#define WHEAT_REDIS_CONNS_SIZE   5
#define WHEAT_MAX_REDIS_CONNS_SIZE   50

#define READER 1
#define WRITER 1
#define TOTAL  1

struct redisConnUnit {
    size_t ack;
    struct client *outer_client;
    struct client *redis_client;
    struct redisInstance *instance;
};

struct redisAppData {
    int count;
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
    unit->outer_client = NULL;
    unit->instance = instance;
    unit->ack = 0;
    unit->redis_client->client_data = unit;
    return unit;
}

static struct redisConnUnit *getRedisUnit(struct redisInstance *instance, struct client *outer_client)
{
    struct redisConnUnit *unit = outer_client->client_data;
    if (unit != NULL)
        return unit;
    if (listLength(instance->free_conns) == 0) {
        unit = _newRedisUnit(instance);
    } else {
        struct listNode *node = listFirst(instance->free_conns);
        unit = listNodeValue(node);
        removeListNode(instance->free_conns, node);
    }
    ASSERT(isOuterClient(outer_client));
    unit->outer_client = outer_client;
    unit->ack++;
    outer_client->client_data = unit;
    return unit;
}

static void redisConnFree(struct redisConnUnit *redis_unit)
{
    if (!--redis_unit->ack)  {
        redis_unit->outer_client = NULL;
        unregisterClientRead(redis_unit->redis_client);
        appendToListTail(redis_unit->instance->free_conns, redis_unit);
    }
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
    int ret;

    wheatLog(WHEAT_DEBUG, "get client %p conn %p", c->client, c);
    if (isOuterClient(c->client)) {
        int nwritted = 0;
        ret = getRedisKey(c, &key);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
        token = hashDispatch(RedisServer, &key);
        while (nwritted < RedisServer->nwriter) {
            instance = getInstance(RedisServer, token->server_idx);
            redis_unit = getRedisUnit(instance, c->client);
            RedisServer->reserved[nwritted++] = redis_unit;
            registerClientRead(redis_unit->redis_client);
            token = token->next_server;
            wheatLog(WHEAT_DEBUG, "get conn %p", c);
        }
        while ((next = redisBodyNext(c)) != NULL) {
            while (nwritted--) {
                redis_unit = RedisServer->reserved[nwritted];
                ret = sendClientData(redis_unit->redis_client, next);
                if (ret == -1)
                    return WHEAT_WRONG;
                wheatLog(WHEAT_DEBUG, "send data");
            }
        }
        return WHEAT_OK;
    } else {
        struct client *redis_client = c->client, *client;
        redis_unit = redis_client->client_data;
        app_data = redis_unit->outer_client->app_private_data;
        wheatLog(WHEAT_DEBUG, "recv data %d", app_data->count);
        if (--app_data->count)
            return WHEAT_OK;
        client = redis_unit->outer_conn->client;
        wheatLog(WHEAT_DEBUG, "get conn %p", redis_unit->outer_conn);
        wheatLog(WHEAT_DEBUG, "get outer client %p redis_client %p", client, redis_client);
        ASSERT(!isOuterClient(client));
        while ((next = redisBodyNext(c)) != NULL) {
            wheatLog(WHEAT_DEBUG, "send data %s", next->data);
            ret = sendClientData(client, next);
            if (ret == -1)
                return WHEAT_WRONG;
        }
        redisConnFree(redis_unit);
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
    long pos = 0, len, mem_len;
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

    RedisServer->nreader = RedisServer->nwriter = RedisServer->nbackup = 1;
    RedisServer->reserved = wmalloc(sizeof(void*)*RedisServer->nbackup);
    return hashUpdate(RedisServer);

cleanup:
    wheatLog(WHEAT_WARNING, "redis init failed");
    if (iter) freeListIterator(iter);
    redisAppDeinit();
    return WHEAT_WRONG;
}
