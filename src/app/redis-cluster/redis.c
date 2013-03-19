#include "../application.h"
#include "../../protocol/redis/proto_redis.h"
#include "redis.h"

struct redisInstance {
    int id;

    struct client *redis_server;
    struct client *client;
};

struct redisServer {
    struct evcenter *center;
    struct redisInstance *instances;
    size_t instance_size;
};

static struct redisServer *RedisServer = NULL;

int redisCall(struct conn *c, void *arg)
{
    struct redisInstance *instance = &RedisServer->instances[0];
    struct slice *next = NULL;
    int ret;

    if (isReqClient(c->client)) {
        instance->client = c->client;
        while ((next = redisBodyNext(c)) != NULL) {
            ret = sendClientData(instance->redis_server, next);
            if (ret == -1)
                return WHEAT_WRONG;
        }
        return registerClientRead(instance->redis_server);
    } else {
        while ((next = redisBodyNext(c)) != NULL) {
            ret = sendClientData(instance->client, next);
            if (ret == -1)
                return WHEAT_WRONG;
        }
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

static struct client *buildRedisConn(wstr ip, int port, struct protocol *p)
{
    struct client *c = buildConn(ip, port, p);
    return c;
}

void redisAppDeinit()
{
    int pos;
    for (pos = 0; pos < RedisServer->instance_size; pos++) {
        freeClient(RedisServer->instances[pos].redis_server);
    }
    wfree(RedisServer);
}

int redisAppInit(struct protocol *ptocol)
{
    struct configuration *conf = getConfiguration("redis-servers");
    long pos = 0, len, mem_len;
    struct client *client = NULL;
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
        client = buildRedisConn(frags[0], atoi(frags[1]), ptocol);
        if (!client)
            goto cleanup;
        RedisServer->instances[pos].redis_server = client;
        RedisServer->instances[pos].client = NULL;
        wstrFreeSplit(frags, count);
        pos++;
        RedisServer->instance_size++;
    }
    freeListIterator(iter);

    RedisServer->center = eventcenterInit(RedisServer->instance_size);

    return WHEAT_OK;

cleanup:
    wheatLog(WHEAT_WARNING, "redis init failed");
    if (iter) freeListIterator(iter);
    redisAppDeinit();
    return WHEAT_WRONG;
}
