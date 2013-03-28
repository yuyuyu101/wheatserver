#include "redis.h"

#define WHEAT_REDIS_CONFIG_MAGIC     "__WheatRedisConfigSet"
#define WHEAT_REDIS_REDIS_SERVER     "__WheatRedisRedisServer"
#define WHEAT_REDIS_REDIS_SERVER_LEN (sizeof(WHEAT_REDIS_REDIS_SERVER)-1)
#define WHEAT_REDIS_REDIS_INSTANCE   "__WheatRedisRedisInstance%lu"
#define WHEAT_REDIS_REDIS_TOKEN      "__WheatRedisRedisToken%lu"

#define WHEAT_REDIS_HSET             "HSET"
#define WHEAT_REDIS_HGET             "HGET"

enum configReadStatus {
    READ_SERVER_MAX_ID,
    READ_SERVER_NBACKUP,
    READ_SERVER_TIMEOUT,
    READ_SERVER_NINSTANCE,
    READ_INSTANCE_ID,
    READ_INSTANCE_IP,
    READ_INSTANCE_PORT,
    READ_TOKEN_POS,
    READ_TOKEN_INSTANCE_ID,
    READ_TOKEN_NEXT_INSTANCE,
};

// CONFIG_WAIT is used to wait possible extra config-server responses,
// if get extra responses, it will show our get config or save config
// may exists problems.
// So if we only use config file and don't save to config-server, we
// will skip SERVE_WAIT
enum configStatus {
    CONFIG_READ,
    CONFIG_SAVE,
    CONFIG_WAIT,
};

struct configServer {
    struct client *config_client;
    // Used in multi ways
    size_t save_count;
    enum configStatus status;
    enum configReadStatus read_status;
    struct redisInstance pending_instance;
    size_t pos;
    int use_redis;
    long serve_wait_milliseconds;
    struct list *pending_conns;
};

static int saveConfigToServer(struct redisServer *server);

struct configServer *configServerCreate(struct client *client, int use_redis)
{
    struct configServer *config_server;
    config_server = wmalloc(sizeof(struct configServer));
    if (!config_server)
        return NULL;
    config_server->config_client = client;
    config_server->pos = 0;
    config_server->use_redis = use_redis;
    config_server->pending_conns = createList();
    config_server->serve_wait_milliseconds = 0;
    config_server->read_status = READ_SERVER_MAX_ID;
    config_server->status = CONFIG_READ;
    registerClientRead(client);
    return config_server;
}

void configServerDealloc(struct configServer *config_server)
{
    listEach(config_server->pending_conns, (void (*)(void*))finishConn);
    freeList(config_server->pending_conns);
    wfree(config_server);
}

static int fillConfigValidate(struct redisServer *server)
{
    int i, count;
    struct token *token;
    struct redisInstance *instance;

    for (i = 0; i < server->ntoken; ++i) {
        token = &server->tokens[i];
        if (token->pos >= server->ntoken ||
                token->instance_id >= server->ntoken ||
                token->next_instance >= server->ntoken) {

            wheatLog(WHEAT_NOTICE,
                    "fillConfigValidate failed: token invalid :%d %d %d",
                    token->pos, token->instance_id, token->next_instance);
            return WHEAT_WRONG;
        }
        instance = arrayIndex(server->instances, token->instance_id);
        instance->ntoken++;
    }

    count = 0;
    for (i = 0; i < narray(server->instances); ++i) {
        instance = arrayIndex(server->instances, i);
        count += instance->ntoken;
    }

    if (count != WHEAT_KEYSPACE) {
        wheatLog(WHEAT_NOTICE,
                "fillConfigValidate failed: the count of instances %d != %d",
                count, WHEAT_KEYSPACE);
        return WHEAT_WRONG;
    }

    return WHEAT_OK;
}

static int getConfigFromRedisDone(struct redisServer *server, int is_sucess)
{
    struct configServer *config_server = server->config_server;

    if (!is_sucess) {
        // If get config from redis failed, and `config-source` is RedisThenFile
        if (!config_server->use_redis) {
            if (configFromFile(server) == WHEAT_WRONG)
                return WHEAT_WRONG;
            // configFromFile not start save to config-server routine
            if (config_server->status != CONFIG_SAVE)
                config_server->status = CONFIG_WAIT;
            return WHEAT_OK;
        } else {
        // If get config from redis failed, and `config-source` is UseRedis
            return WHEAT_WRONG;
        }
    }

    config_server->status = CONFIG_WAIT;
    config_server->serve_wait_milliseconds = Server.cron_time.tv_sec * 1000 + Server.cron_time.tv_usec / 1000;
    return WHEAT_OK;
}

static int sendCommand(struct configServer *config_server, const char *command,
        const char *key, size_t key_len, char *field, size_t field_len,
        char *value, size_t val_len)
{
    struct conn *send_conn;
    struct slice next;
    char packet[255];
    int ret;
    if (value != NULL && val_len != 0) {
        ret = snprintf(packet, sizeof(packet),
                "*4\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n",
                strlen(command), command, key_len, key, field_len, field,
                val_len, value);
        config_server->save_count++;
    } else {
        ASSERT(key_len==strlen(key)&&field_len==strlen(field));
        ret = snprintf(packet, sizeof(packet),
                "*3\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n",
                strlen(command), command, key_len, key, field_len, field);
    }

    if (ret < 0 || ret > sizeof(packet))
        return WHEAT_WRONG;
    sliceTo(&next, (uint8_t*)packet, ret);
    send_conn = connGet(config_server->config_client);
    ret = syncWriteBulkTo(send_conn->client->clifd, &next);
    ASSERT(ret == next.len);
    if (ret == -1) {
        return WHEAT_WRONG;
    }
    finishConn(send_conn);
    return WHEAT_OK;
}

static int saveConfigToServer(struct redisServer *server)
{
    int i, field_len, val_len, key_len, ret;
    struct redisInstance *instance;
    struct token *token;
    struct configServer *config_server;
    char field[20];
    char val[100];
    char key[100];

    config_server = server->config_server;
    ASSERT(config_server);

    field_len = snprintf(field, sizeof(field), "max_id");
    val_len = snprintf(val, sizeof(val), "%lu", server->max_id);
    ret = sendCommand(config_server, WHEAT_REDIS_HSET, WHEAT_REDIS_REDIS_SERVER,
            WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, val, val_len);
    if (ret == WHEAT_WRONG)
        goto failed;

    field_len = snprintf(field, sizeof(field), "nbackup");
    val_len = snprintf(val, sizeof(val), "%lu", server->nbackup);
    ret = sendCommand(config_server, WHEAT_REDIS_HSET, WHEAT_REDIS_REDIS_SERVER,
            WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, val, val_len);
    if (ret == WHEAT_WRONG)
        goto failed;

    field_len = snprintf(field, sizeof(field), "timeout");
    val_len = snprintf(val, sizeof(val), "%lu", server->timeout);
    ret = sendCommand(config_server, WHEAT_REDIS_HSET, WHEAT_REDIS_REDIS_SERVER,
            WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, val, val_len);
    if (ret == WHEAT_WRONG)
        goto failed;

    field_len = snprintf(field, sizeof(field), "ninstance");
    val_len = snprintf(val, sizeof(val), "%lu", narray(server->instances));
    ret = sendCommand(config_server, WHEAT_REDIS_HSET, WHEAT_REDIS_REDIS_SERVER,
            WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, val, val_len);
    if (ret == WHEAT_WRONG)
        goto failed;

    for (i = 0; i < narray(server->instances); ++i) {
        instance = arrayIndex(server->instances, i);
        key_len = snprintf(key, sizeof(key), WHEAT_REDIS_REDIS_INSTANCE, instance->id);

        field_len = snprintf(field, sizeof(field), "id");
        val_len = snprintf(val, sizeof(val), "%lu", instance->id);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;

        field_len = snprintf(field, sizeof(field), "ip");
        val_len = snprintf(val, sizeof(val), "%s", instance->ip);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;

        field_len = snprintf(field, sizeof(field), "port");
        val_len = snprintf(val, sizeof(val), "%d", instance->port);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;
    }

    for (i = 0; i < server->ntoken; ++i) {
        token = &server->tokens[i];
        key_len = snprintf(key, sizeof(key), WHEAT_REDIS_REDIS_TOKEN, token->pos);

        field_len = snprintf(field, sizeof(field), "pos");
        val_len = snprintf(val, sizeof(val), "%lu", token->pos);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;

        field_len = snprintf(field, sizeof(field), "instance_id");
        val_len = snprintf(val, sizeof(val), "%lu", token->instance_id);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;

        field_len = snprintf(field, sizeof(field), "next_instance");
        val_len = snprintf(val, sizeof(val), "%lu", token->next_instance);
        ret = sendCommand(config_server, WHEAT_REDIS_HSET, key,
                key_len, field, field_len, val, val_len);
        if (ret == WHEAT_WRONG)
            goto failed;
    }

    return WHEAT_OK;

failed:
    return WHEAT_WRONG;
}

int getServerFromRedis(struct redisServer *server)
{
    int field_len, ret;
    char field[20];
    struct configServer *config_server;

    config_server = server->config_server;
    switch (config_server->read_status) {
        case READ_SERVER_MAX_ID:
            field_len = snprintf(field, sizeof(field), "max_id");
            ret = sendCommand(config_server, WHEAT_REDIS_HGET, WHEAT_REDIS_REDIS_SERVER,
                    WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, NULL, 0);
            if (ret == WHEAT_WRONG)
                goto failed;
            break;

        case READ_SERVER_NBACKUP:
            field_len = snprintf(field, sizeof(field), "nbackup");
            ret = sendCommand(config_server, WHEAT_REDIS_HGET, WHEAT_REDIS_REDIS_SERVER,
                    WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, NULL, 0);
            if (ret == WHEAT_WRONG)
                goto failed;
            break;

        case READ_SERVER_TIMEOUT:
            field_len = snprintf(field, sizeof(field), "timeout");
            ret = sendCommand(config_server, WHEAT_REDIS_HGET, WHEAT_REDIS_REDIS_SERVER,
                    WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, NULL, 0);
            if (ret == WHEAT_WRONG)
                goto failed;
            break;

        case READ_SERVER_NINSTANCE:
            field_len = snprintf(field, sizeof(field), "ninstance");
            ret = sendCommand(config_server, WHEAT_REDIS_HGET, WHEAT_REDIS_REDIS_SERVER,
                    WHEAT_REDIS_REDIS_SERVER_LEN, field, field_len, NULL, 0);
            if (ret == WHEAT_WRONG)
                goto failed;
            break;

        default:
            wheatLog(WHEAT_WARNING, "No way to reach here");
            goto failed;
    }
    return WHEAT_OK;
failed:
    return getConfigFromRedisDone(server, 0);
}

static int getRedisInstanceFromRedis(struct configServer *config_server,
        char *field, size_t field_len, size_t pos)
{
    int ret, key_len;
    char key[100];

    key_len = snprintf(key, sizeof(key), WHEAT_REDIS_REDIS_INSTANCE, pos);

    ret = sendCommand(config_server, WHEAT_REDIS_HGET, key,
            key_len, field, field_len, NULL, 0);
    if (ret == WHEAT_WRONG)
        goto failed;

    return WHEAT_OK;
failed:
    return WHEAT_WRONG;
}

static int getTokenFromRedis(struct configServer *config_server, char *field,
        size_t field_len, size_t pos)
{
    int ret, key_len;
    char key[100];

    key_len = snprintf(key, sizeof(key), WHEAT_REDIS_REDIS_TOKEN, pos);

    ret = sendCommand(config_server, WHEAT_REDIS_HGET, key,
            key_len, field, field_len, NULL, 0);
    if (ret == WHEAT_WRONG)
        goto failed;

    return WHEAT_OK;
failed:
    return WHEAT_WRONG;
}

static int getStrFrom(wstr body, wstr *out)
{
    int len;
    if (body[0] == '-')
        return WHEAT_WRONG;
    // Suppose the length of value must less than 9, and it couldn't
    // larger than 9
    len = atoi(&body[1]);
    *out = wstrNewLen(&body[4], len);
    return WHEAT_OK;
}

static int getValFrom(wstr body, size_t *out)
{
    long long long_val;
    int len;
    wstr value;
    if (body[1] == '-')
        return WHEAT_WRONG;
    // Suppose the length of value must less than 9, and it couldn't
    // larger than 9
    len = atoi(&body[1]);
    value = wstrNewLen(&body[4], len);
    if (string2ll(value, wstrlen(value), &long_val) == WHEAT_WRONG)
        return WHEAT_WRONG;
    wstrFree(value);
    if (out)
        *out = long_val;
    return WHEAT_OK;
}

// If handleConfigRead failed and return WHEAT_WRONG, all initial value from
// redis should be cleaned up
int handleConfigRead(struct redisServer *server, struct conn *c, wstr body)
{
    struct redisInstance *instance_p;
    struct redisInstance *pending_instance_p;
    struct configServer *config_server;
    struct token *token;
    int ret;

    ret = WHEAT_WRONG;
    config_server = server->config_server;

    pending_instance_p = &config_server->pending_instance;
    switch(config_server->read_status) {
        // Read config for RedisServer
        case READ_SERVER_MAX_ID:
            if (getValFrom(body, &server->max_id) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_SERVER_NBACKUP;
            if (getServerFromRedis(server) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_SERVER_NBACKUP:
            if (getValFrom(body, &server->nbackup) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_SERVER_TIMEOUT;
            if (getServerFromRedis(server) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_SERVER_TIMEOUT:
            if (getValFrom(body, (size_t*)&server->timeout) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_SERVER_NINSTANCE;
            if (getServerFromRedis(server) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_SERVER_NINSTANCE:
            if (getValFrom(body, &server->live_instances) == WHEAT_WRONG)
                goto cleanup;
            if (getRedisInstanceFromRedis(config_server, "id", 2,
                        config_server->pos) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_INSTANCE_ID;
            break;

            // Read config for RedisServer->instances
        case READ_INSTANCE_ID:
            if (getValFrom(body, &pending_instance_p->id) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_INSTANCE_IP;
            if (getRedisInstanceFromRedis(config_server, "ip", 2,
                        config_server->pos) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_INSTANCE_IP:
            if (getStrFrom(body, &pending_instance_p->ip) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_INSTANCE_PORT;
            if (getRedisInstanceFromRedis(config_server, "port", 4,
                        config_server->pos) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_INSTANCE_PORT:
            if (getValFrom(body, (size_t*)&pending_instance_p->port) == WHEAT_WRONG)
                goto cleanup;
            arrayPush(server->instances, pending_instance_p);
            instance_p = arrayLast(server->instances);
            if (initInstance(instance_p, pending_instance_p->id,
                        pending_instance_p->ip,
                        pending_instance_p->port, NONDIRTY) == WHEAT_WRONG)
                goto cleanup;

            if (++config_server->pos < server->live_instances){
                if (getRedisInstanceFromRedis(config_server, "id", 2,
                            config_server->pos) == WHEAT_WRONG)
                    goto cleanup;
                config_server->read_status = READ_INSTANCE_ID;
            } else {
                config_server->pos = 0;
                if (getTokenFromRedis(config_server, "pos", 3, config_server->pos) == WHEAT_WRONG)
                    goto cleanup;
                config_server->read_status = READ_TOKEN_POS;
            }
            break;

            // Read config for RedisServer->tokens
        case READ_TOKEN_POS:
            token = &server->tokens[config_server->pos];
            if (getValFrom(body, &token->pos) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_TOKEN_INSTANCE_ID;
            if (getTokenFromRedis(config_server, "instance_id", 11, config_server->pos) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_TOKEN_INSTANCE_ID:
            token = &server->tokens[config_server->pos];
            if (getValFrom(body, &token->instance_id) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_TOKEN_NEXT_INSTANCE;
            if (getTokenFromRedis(config_server, "next_instance", 13, config_server->pos) == WHEAT_WRONG)
                goto cleanup;
            break;

        case READ_TOKEN_NEXT_INSTANCE:
            token = &server->tokens[config_server->pos];
            if (getValFrom(body, &token->next_instance) == WHEAT_WRONG)
                goto cleanup;
            config_server->read_status = READ_TOKEN_NEXT_INSTANCE;
            if (++config_server->pos < server->ntoken) {
                if (getTokenFromRedis(config_server, "pos", 3, config_server->pos) == WHEAT_WRONG)
                    goto cleanup;
                config_server->read_status = READ_TOKEN_POS;
            } else {
                if (fillConfigValidate(server) == WHEAT_WRONG)
                    goto cleanup;
                wheatLog(WHEAT_NOTICE, "get config from redis server sucessful");
                if (getConfigFromRedisDone(server, 1) == WHEAT_WRONG)
                    goto cleanup;
            }
    }
    ret = WHEAT_OK;

cleanup:
    wstrFree(body);
    finishConn(c);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_NOTICE, "get config from redis failed");
        return getConfigFromRedisDone(server, 0);
    }
    return WHEAT_OK;
}

int configFromFile(struct redisServer *server)
{
    int pos, count, ret;
    long len;
    struct listIterator *iter = NULL;
    struct redisInstance ins, *instance;
    struct configuration *conf;
    wstr *frags = NULL;

    conf = getConfiguration("redis-servers");
    if (!conf->target.ptr)
        return WHEAT_WRONG;

    len = listLength((struct list *)(conf->target.ptr));
    struct listNode *node = NULL;
    iter = listGetIterator(conf->target.ptr, START_HEAD);
    count = 0;
    pos = 0;
    while ((node = listNext(iter)) != NULL) {
        frags = wstrNewSplit(listNodeValue(node), ":", 1, &count);

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

    ret = hashInit(server);
    if (ret == WHEAT_WRONG)
        return WHEAT_WRONG;

    wheatLog(WHEAT_NOTICE, "get config from file successful");
    // Only when specify RedisThenFile and get info from redis server failed,
    // we will save all config to redis server
    if (server->config_server && server->config_server->config_client) {
        ret = saveConfigToServer(server);
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_WARNING, "Save config to redis server failed %s:%d",
                    server->config_server->config_client->ip,
                    server->config_server->config_client->port);
        } else {
            server->config_server->status = CONFIG_SAVE;
        }
    } else {
        wheatLog(WHEAT_NOTICE, "Not save config file to redis server");
    }

    return WHEAT_OK;

cleanup:
    if (iter) freeListIterator(iter);
    if (frags) wstrFreeSplit(frags, count);
    wheatLog(WHEAT_WARNING, "redis init failed");
    return WHEAT_WRONG;
}

static int handleConfigSave(struct redisServer *server, struct conn *c, wstr body)
{
    struct configServer *config_server;

    config_server = server->config_server;

    if (wstrCmpChars(body, ":1\r\n", 4)) {
        wheatLog(WHEAT_NOTICE, "save config file error: %s", body);
    }
    if (--config_server->save_count == 0) {
        wheatLog(WHEAT_NOTICE, "Save config to redis success");
        config_server->status = CONFIG_WAIT;
        config_server->serve_wait_milliseconds = Server.cron_time.tv_sec * 1000 + Server.cron_time.tv_usec / 1000;
    }
    return WHEAT_OK;
}

int isStartServe(struct redisServer *server)
{
    struct configServer *config_server;
    long millisecond_now = Server.cron_time.tv_sec * 1000 + Server.cron_time.tv_usec /1000;
    config_server = server->config_server;
    if (config_server->status != CONFIG_WAIT)
        return 0;
    if (millisecond_now - config_server->serve_wait_milliseconds > WHEAT_SERVE_WAIT_MILLISECONDS)
        return 1;
    else if (config_server->serve_wait_milliseconds == 0)
        // when skip SERVE_WAIT and configFromFile use
        return 1;
    return 0;
}

int handleConfig(struct redisServer *server, struct conn *c)
{
    struct configServer *config_server = server->config_server;
    wstr body;
    struct slice *next;

    if (c->client != config_server->config_client) {
        appendToListTail(server->pending_conns, c);
        return WHEAT_OK;
    }

    body = wstrEmpty();
    while ((next = redisBodyNext(c)) != NULL) {
        body = wstrCatLen(body, (const char *)next->data, next->len);
    }

    switch(config_server->status) {
        case CONFIG_READ:
            return handleConfigRead(server, c, body);
        case CONFIG_SAVE:
            return handleConfigSave(server, c, body);
        case CONFIG_WAIT:
            wheatLog(WHEAT_WARNING, "Exists config-server connection, failed");
            return WHEAT_WRONG;
        default:
            wheatLog(WHEAT_WARNING, "No one will reach here");
            return WHEAT_WRONG;

    }
    wheatLog(WHEAT_WARNING, "No one will reach here");
    return WHEAT_WRONG;
}
