#include "../application.h"
#include "../../protocol/redis/proto_redis.h"
#include "redis.h"

#define WHEAT_REDIS_CONNS_SIZE   5
#define WHEAT_MAX_REDIS_CONNS_SIZE   50

struct redisConnUnit {
    size_t ack;
    struct client *client;
    struct client *redis_client;
    struct redisInstance *instance;
};

struct redisInstance {
    int id;
    wstr ip;
    int port;

    struct array *server_conns;
    struct list *free_conns;
};

struct redisServer {
    struct redisInstance *instances;
    size_t instance_size;
};

static struct redisServer *RedisServer = NULL;
static struct protocol *RedisProtocol = NULL;

static uint32_t simpleHash(struct slice *key, int max)
{
    return dictGenHashFunction(key->data, key->len) % max;
}

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
    unit->client = NULL;
    unit->instance = instance;
    unit->ack = 0;
    return unit;
}

static struct redisConnUnit *getRedisUnit(struct redisInstance *instance, struct client *client)
{
    if (client->client_data != NULL)
        return client->client_data;
    struct redisConnUnit *unit = NULL;
    if (listLength(instance->free_conns) == 0) {
        unit = _newRedisUnit(instance);
    } else {
        struct listNode *node = listFirst(instance->free_conns);
        unit = listNodeValue(node);
        removeListNode(instance->free_conns, node);
    }
    unit->client = client;
    unit->ack++;
    client->client_data = unit->redis_client->client_data = unit;
    return unit;
}

static void redisConnFree(struct redisConnUnit *redis_unit)
{
    if (!--redis_unit->ack)  {
        unregisterClientRead(redis_unit->redis_client);
        appendToListTail(redis_unit->instance->free_conns, redis_unit);
    }
}

int redisCall(struct conn *c, void *arg)
{
    struct redisInstance *instance = NULL;
    struct slice *next = NULL;
    struct redisConnUnit *redis_unit = NULL;
    struct slice key;
    int ret;

    if (isOuterClient(c->client)) {
        ret = getRedisKey(c, &key);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
        uint32_t pos = simpleHash(&key, RedisServer->instance_size);
        instance = &RedisServer->instances[pos];
        redis_unit = getRedisUnit(instance, c->client);
        while ((next = redisBodyNext(c)) != NULL) {
            ret = sendClientData(redis_unit->redis_client, next);
            if (ret == -1)
                return WHEAT_WRONG;
        }
        return registerClientRead(redis_unit->redis_client);
    } else {
        struct client *redis_client = c->client;
        redis_unit = redis_client->client_data;
        while ((next = redisBodyNext(c)) != NULL) {
            ret = sendClientData(redis_unit->client, next);
            if (ret == -1)
                return WHEAT_WRONG;
        }
        redisConnFree(redis_unit);
        return WHEAT_OK;
    }
}

void *redisAppDataInit(struct conn *c)
{
    return NULL;
}

void redisAppDataDeinit(void *data)
{
    return ;
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

    return WHEAT_OK;

cleanup:
    wheatLog(WHEAT_WARNING, "redis init failed");
    if (iter) freeListIterator(iter);
    redisAppDeinit();
    return WHEAT_WRONG;
}
