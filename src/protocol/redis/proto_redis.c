// Redis protocol parse implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "../protocol.h"
#include "../str_macro.h"
#include "proto_redis.h"

#define CR                  (uint8_t)13
#define LF                  (uint8_t)10

#define REDIS_FINISHED(r)   (((r)->stage) == MES_END)

int redisSpot(struct conn *c);
int parseRedis(struct conn *c, struct slice *slice, size_t *);
void *initRedisData();
void freeRedisData(void *d);
int initRedis();
void deallocRedis();

static struct protocol ProtocolRedis = {
    redisSpot, parseRedis, initRedisData, freeRedisData,
        initRedis, deallocRedis,
};

struct moduleAttr ProtocolRedisAttr = {
    "Redis", PROTOCOL, {.protocol=&ProtocolRedis}, NULL, 0, NULL, 0, NULL, 0
};

static wstr RedisHashTag = NULL;

enum redisCommand {
    REDIS_EXISTS,
    REDIS_PTTL,
    REDIS_TTL,
    REDIS_TYPE,
    REDIS_BITCOUNT,
    REDIS_GET,
    REDIS_GETBIT,
    REDIS_GETRANGE,
    REDIS_GETSET,
    REDIS_STRLEN,
    REDIS_HEXISTS,
    REDIS_HGET,
    REDIS_HGETALL,
    REDIS_HKEYS,
    REDIS_HLEN,
    REDIS_HVALS,
    REDIS_LINDEX,                 /* redis requests - lists */
    REDIS_LLEN,
    REDIS_LRANGE,
    REDIS_SCARD,
    REDIS_SISMEMBER,
    REDIS_SMEMBERS,
    REDIS_SRANDMEMBER,
    REDIS_ZCARD,
    REDIS_ZCOUNT,
    REDIS_ZRANGE,
    REDIS_ZRANGEBYSCORE,
    REDIS_ZREVRANGE,
    REDIS_ZREVRANGEBYSCORE,
    REDIS_ZREVRANK,
    REDIS_ZSCORE,

    REDIS_DEL,                    /* redis commands - keys */
    REDIS_EXPIRE,
    REDIS_EXPIREAT,
    REDIS_PEXPIRE,
    REDIS_PEXPIREAT,
    REDIS_PERSIST,
    REDIS_APPEND,                 /* redis requests - string */
    REDIS_DECR,
    REDIS_DECRBY,
    REDIS_INCR,
    REDIS_INCRBY,
    REDIS_INCRBYFLOAT,
    REDIS_PSETEX,
    REDIS_SET,
    REDIS_SETBIT,
    REDIS_SETEX,
    REDIS_SETNX,
    REDIS_SETRANGE,
    REDIS_HDEL,                   /* redis requests - hashes */
    REDIS_HINCRBY,
    REDIS_HINCRBYFLOAT,
    REDIS_HSET,
    REDIS_HSETNX,
    REDIS_LINSERT,
    REDIS_LPOP,
    REDIS_LPUSH,
    REDIS_LPUSHX,
    REDIS_LREM,
    REDIS_LSET,
    REDIS_LTRIM,
    REDIS_RPOP,
    REDIS_RPUSH,
    REDIS_RPUSHX,
    REDIS_SADD,                   /* redis requests - sets */
    REDIS_SPOP,
    REDIS_SREM,
    REDIS_ZADD,                   /* redis requests - sorted sets */
    REDIS_ZINCRBY,
    REDIS_ZRANK,
    REDIS_ZREM,
    REDIS_ZREMRANGEBYRANK,
    REDIS_ZREMRANGEBYSCORE,
    REDIS_UNKNOWN
};

enum reqStage {
    MES_START = 1,
    REQ_GET_ARGS,
    REQ_GET_ARG_LEN_LF,
    REQ_GET_ARG_DOLLAR,
    REQ_GET_ARG_LEN,
    REQ_GET_ARG_VAL_LF,
    REQ_GET_ARG_COMMAND_OR_KEY,
    REQ_GET_ARG_VAL,
    RES_SINGLE,
    RES_SINGLE_LF,
    RES_GET_ARGS,
    RES_GET_ARG_LEN,
    RES_GET_ARG_LEN_LF,
    RES_GET_ARG_VAL_LF,
    RES_GET_ARG_PREFIX,
    RES_GET_ARG_VAL,
    RES_NIL_VAL,
    RES_NIL_VAL_LF,
    MES_END,
    MES_ERR
};

// Redis protocol request format
// *3\r\n$3\r\nset\r\n$3\r\nkey\r\n$5\r\nvalue\r\n
//                             |
//                             |
//                             |
//                             |
//                        key_end_pos(\r)
struct redisProcData {
    int curr_arg_len;
    int curr_arg;
    int args;
    wstr key;
    wstr command;
    size_t key_end_pos;
    enum reqStage stage;
    enum redisCommand command_type;
    int is_read;

    struct array *req_body;
    size_t pos;
};

static int redisCommandHandle(struct redisProcData *redis_data,
        wstr command)
{
    uint8_t *c;
    int args;

    args = wstrlen(command);
    c = (uint8_t *)command;
    switch (args) {
        case 3:
            if (str3icmp(c, 'd', 'e', 'l'))
                return REDIS_DEL;
            if (str3icmp(c, 'g', 'e', 't'))
                return REDIS_GET;
            if (str3icmp(c, 's', 'e', 't'))
                return REDIS_SET;
            if (str3icmp(c, 't', 't', 'l'))
                return REDIS_TTL;

            break;
        case 4:
            if (str4icmp(c, 'p', 't', 't', 'l'))
                return REDIS_PTTL;

            if (str4icmp(c, 'd', 'e', 'c', 'r'))
                return REDIS_DECR;

            if (str4icmp(c, 'h', 'd', 'e', 'l'))
                return REDIS_HDEL;

            if (str4icmp(c, 'h', 'g', 'e', 't'))
                return REDIS_HGET;

            if (str4icmp(c, 'h', 'l', 'e', 'n'))
                return REDIS_HLEN;

            if (str4icmp(c, 'h', 's', 'e', 't'))
                return REDIS_HSET;

            if (str4icmp(c, 'i', 'n', 'c', 'r'))
                return REDIS_INCR;

            if (str4icmp(c, 'l', 'l', 'e', 'n'))
                return REDIS_LLEN;

            if (str4icmp(c, 'l', 'p', 'o', 'p'))
                return REDIS_LPOP;

            if (str4icmp(c, 'l', 'r', 'e', 'm'))
                return REDIS_LREM;

            if (str4icmp(c, 'l', 's', 'e', 't'))
                return REDIS_LSET;

            if (str4icmp(c, 'r', 'p', 'o', 'p'))
                return REDIS_RPOP;

            if (str4icmp(c, 's', 'a', 'd', 'd'))
                return REDIS_SADD;

            if (str4icmp(c, 's', 'p', 'o', 'p'))
                return REDIS_SPOP;

            if (str4icmp(c, 's', 'r', 'e', 'm'))
                return REDIS_SREM;

            if (str4icmp(c, 't', 'y', 'p', 'e'))
                return REDIS_TYPE;

            if (str4icmp(c, 'z', 'a', 'd', 'd'))
                return REDIS_ZADD;

            if (str4icmp(c, 'z', 'r', 'e', 'm'))
                return REDIS_ZREM;

            break;

        case 5:
            if (str5icmp(c, 'h', 'k', 'e', 'y', 's'))
                return REDIS_HKEYS;

            if (str5icmp(c, 'h', 'v', 'a', 'l', 's'))
                return REDIS_HVALS;

            if (str5icmp(c, 'l', 'p', 'u', 's', 'h'))
                return REDIS_LPUSH;

            if (str5icmp(c, 'l', 't', 'r', 'i', 'm'))
                return REDIS_LTRIM;

            if (str5icmp(c, 'r', 'p', 'u', 's', 'h'))
                return REDIS_RPUSH;

            if (str5icmp(c, 's', 'c', 'a', 'r', 'd'))
                return REDIS_SCARD;

            if (str5icmp(c, 's', 'e', 't', 'e', 'x'))
                return REDIS_SETEX;

            if (str5icmp(c, 's', 'e', 't', 'n', 'x'))
                return REDIS_SETNX;

            if (str5icmp(c, 'z', 'c', 'a', 'r', 'd'))
                return REDIS_ZCARD;

            if (str5icmp(c, 'z', 'r', 'a', 'n', 'k'))
                return REDIS_ZRANK;

            break;

        case 6:
            if (str6icmp(c, 'a', 'p', 'p', 'e', 'n', 'd'))
                return REDIS_APPEND;

            if (str6icmp(c, 'd', 'e', 'c', 'r', 'b', 'y'))
                return REDIS_DECRBY;

            if (str6icmp(c, 'e', 'x', 'i', 's', 't', 's'))
                return REDIS_EXISTS;

            if (str6icmp(c, 'e', 'x', 'p', 'i', 'r', 'e'))
                return REDIS_EXPIRE;

            if (str6icmp(c, 'g', 'e', 't', 'b', 'i', 't'))
                return REDIS_GETBIT;

            if (str6icmp(c, 'g', 'e', 't', 's', 'e', 't'))
                return REDIS_GETSET;

            if (str6icmp(c, 'p', 's', 'e', 't', 'e', 'x'))
                return REDIS_PSETEX;

            if (str6icmp(c, 'h', 's', 'e', 't', 'n', 'x'))
                return REDIS_HSETNX;

            if (str6icmp(c, 'i', 'n', 'c', 'r', 'b', 'y'))
                return REDIS_INCRBY;

            if (str6icmp(c, 'l', 'i', 'n', 'd', 'e', 'x'))
                return REDIS_LINDEX;

            if (str6icmp(c, 'l', 'p', 'u', 's', 'h', 'x'))
                return REDIS_LPUSHX;

            if (str6icmp(c, 'l', 'r', 'a', 'n', 'g', 'e'))
                return REDIS_LRANGE;

            if (str6icmp(c, 'r', 'p', 'u', 's', 'h', 'x'))
                return REDIS_RPUSHX;

            if (str6icmp(c, 's', 'e', 't', 'b', 'i', 't'))
                return REDIS_SETBIT;

            if (str6icmp(c, 's', 't', 'r', 'l', 'e', 'n'))
                return REDIS_STRLEN;

            if (str6icmp(c, 'z', 'c', 'o', 'u', 'n', 't'))
                return REDIS_ZCOUNT;

            if (str6icmp(c, 'z', 'r', 'a', 'n', 'g', 'e'))
                return REDIS_ZRANGE;

            if (str6icmp(c, 'z', 's', 'c', 'o', 'r', 'e'))
                return REDIS_ZSCORE;

            break;

        case 7:
            if (str7icmp(c, 'p', 'e', 'r', 's', 'i', 's', 't'))
                return REDIS_PERSIST;

            if (str7icmp(c, 'p', 'e', 'x', 'p', 'i', 'r', 'e'))
                return REDIS_PEXPIRE;

            if (str7icmp(c, 'h', 'e', 'x', 'i', 's', 't', 's'))
                return REDIS_HEXISTS;

            if (str7icmp(c, 'h', 'g', 'e', 't', 'a', 'l', 'l'))
                return REDIS_HGETALL;

            if (str7icmp(c, 'h', 'i', 'n', 'c', 'r', 'b', 'y'))
                return REDIS_HINCRBY;

            if (str7icmp(c, 'l', 'i', 'n', 's', 'e', 'r', 't'))
                return REDIS_LINSERT;

            if (str7icmp(c, 'z', 'i', 'n', 'c', 'r', 'b', 'y'))
                return REDIS_ZINCRBY;

            break;

        case 8:
            if (str8icmp(c, 'e', 'x', 'p', 'i', 'r', 'e', 'a', 't'))
                return REDIS_EXPIREAT;

            if (str8icmp(c, 'b', 'i', 't', 'c', 'o', 'u', 'n', 't'))
                return REDIS_BITCOUNT;

            if (str8icmp(c, 'g', 'e', 't', 'r', 'a', 'n', 'g', 'e'))
                return REDIS_GETRANGE;

            if (str8icmp(c, 's', 'e', 't', 'r', 'a', 'n', 'g', 'e'))
                return REDIS_SETRANGE;

            if (str8icmp(c, 's', 'm', 'e', 'm', 'b', 'e', 'r', 's'))
                return REDIS_SMEMBERS;

            if (str8icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'k'))
                return REDIS_ZREVRANK;

            break;

        case 9:
            if (str9icmp(c, 'p', 'e', 'x', 'p', 'i', 'r', 'e', 'a', 't'))
                return REDIS_PEXPIREAT;

            if (str9icmp(c, 's', 'i', 's', 'm', 'e', 'm', 'b', 'e', 'r'))
                return REDIS_SISMEMBER;

            if (str9icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'g', 'e'))
                return REDIS_ZREVRANGE;

            break;

        case 11:
            if (str11icmp(c, 'i', 'n', 'c', 'r', 'b', 'y', 'f', 'l', 'o', 'a', 't'))
                return REDIS_INCRBYFLOAT;

            if (str11icmp(c, 's', 'r', 'a', 'n', 'd', 'm', 'e', 'm', 'b', 'e', 'r'))
                return REDIS_SRANDMEMBER;

            break;

        case 12:
            if (str12icmp(c, 'h', 'i', 'n', 'c', 'r', 'b', 'y', 'f', 'l', 'o', 'a', 't'))
                return REDIS_HINCRBYFLOAT;

            break;

        case 13:
            if (str13icmp(c, 'z', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REDIS_ZRANGEBYSCORE;

            break;

        case 15:
            if (str15icmp(c, 'z', 'r', 'e', 'm', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 'r', 'a', 'n', 'k'))
                return REDIS_ZREMRANGEBYRANK;

            break;

        case 16:
            if (str16icmp(c, 'z', 'r', 'e', 'm', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REDIS_ZREMRANGEBYSCORE;

            if (str16icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REDIS_ZREVRANGEBYSCORE;

            break;
    }
    return REDIS_UNKNOWN;
}

static ssize_t redisResParser(struct redisProcData *redis_data, struct slice *s)
{
    char ch;
    size_t pos = 0;
    while (pos < s->len) {
        ch = s->data[pos];
        switch (redis_data->stage) {
            case MES_START:
                if (ch == '+' || ch == '-' || ch == ':') {
                    redis_data->stage = RES_SINGLE;
                    redis_data->args = 1;
                } else if (ch == '$') {
                    redis_data->stage = RES_GET_ARG_LEN;
                    redis_data->args = 1;
                } else if (ch == '*') {
                    redis_data->stage = RES_GET_ARGS;
                } else
                    goto redis_err;
                pos++;
                break;
            case RES_SINGLE:
                if (ch == CR) {
                    redis_data->stage = RES_SINGLE_LF;
                    redis_data->curr_arg++;
                }
                pos++;
                break;
            case RES_SINGLE_LF:
                if (ch == LF) {
                    if (redis_data->curr_arg < redis_data->args) {
                        redis_data->stage = RES_GET_ARG_PREFIX;
                    } else {
                        redis_data->stage = MES_END;
                    }
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case RES_GET_ARGS:
                if (isdigit(ch)) {
                    redis_data->args = redis_data->args * 10 + (ch - '0');
                    if (redis_data->args < 2)
                        goto redis_err;
                } else if (ch == CR) {
                    redis_data->stage = RES_GET_ARG_LEN_LF;
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case RES_GET_ARG_LEN_LF:
                if (ch == LF) {
                    if (redis_data->curr_arg < redis_data->args) {
                        redis_data->stage = RES_GET_ARG_PREFIX;
                    } else {
                        redis_data->stage = MES_END;
                    }
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case RES_GET_ARG_PREFIX:
                if (ch == '$') {
                    redis_data->stage = RES_GET_ARG_LEN;
                } else if (ch == ':') {
                    redis_data->stage = RES_SINGLE;
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case RES_NIL_VAL:
                if (ch == CR) {
                    redis_data->curr_arg++;
                    redis_data->stage = RES_GET_ARG_LEN_LF;
                }
                pos++;
                break;
            case RES_GET_ARG_LEN:
                if (isdigit(ch)) {
                    redis_data->curr_arg_len *= 10;
                    redis_data->curr_arg_len += (ch - '0');
                } else if (ch == CR) {
                    redis_data->stage = RES_GET_ARG_VAL_LF;
                } else if (ch == '-') {
                    redis_data->stage = RES_NIL_VAL;
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case RES_GET_ARG_VAL_LF:
                if (ch == LF) {
                    redis_data->stage = RES_GET_ARG_VAL;
                    pos++;
                } else {
                    goto redis_err;
                }
                break;
            case RES_GET_ARG_VAL:
                if (ch == CR) {
                    redis_data->stage = RES_GET_ARG_LEN_LF;
                    pos++;
                    break;
                }
                if (redis_data->curr_arg_len + pos > s->len) {
                    redis_data->curr_arg_len -= (s->len-pos);
                    pos = s->len;
                    return pos;
                }
                pos += redis_data->curr_arg_len;
                redis_data->curr_arg++;
                redis_data->curr_arg_len = 0;
                break;
            case MES_END:
                return pos;
            default:
                goto redis_err;
        }
    }
    return pos;

redis_err:
    redis_data->stage = MES_ERR;
    return -1;
}

static ssize_t redisReqParser(struct redisProcData *redis_data, struct slice *s)
{
    enum redisCommand command_type;
    char ch;
    size_t pos = 0;
    while (pos < s->len) {
        ch = s->data[pos];
        switch (redis_data->stage) {
            case MES_START:
                if (ch != '*')
                    goto redis_err;
                redis_data->stage = REQ_GET_ARGS;
                pos++;
                break;
            case REQ_GET_ARGS:
                if (isdigit(ch)) {
                    redis_data->args = redis_data->args * 10 + (ch - '0');
                    if (redis_data->args < 1)
                        goto redis_err;
                    pos++;
                } else if (ch == CR) {
                    redis_data->stage = REQ_GET_ARG_LEN_LF;
                    pos++;
                } else {
                    goto redis_err;
                }
                break;
            case REQ_GET_ARG_LEN_LF:
                if (ch == LF) {
                    if (redis_data->curr_arg < redis_data->args) {
                        redis_data->stage = REQ_GET_ARG_DOLLAR;
                    } else {
                        redis_data->stage = MES_END;
                    }
                } else {
                    goto redis_err;
                }
                pos++;
                break;
            case REQ_GET_ARG_DOLLAR:
                if (ch == '$') {
                    redis_data->stage = REQ_GET_ARG_LEN;
                    pos++;
                } else {
                    goto redis_err;
                }
                break;
            case REQ_GET_ARG_LEN:
                if (isdigit(ch)) {
                    redis_data->curr_arg_len *= 10;
                    redis_data->curr_arg_len += (ch - '0');
                    pos++;
                } else if (ch == CR) {
                    redis_data->stage = REQ_GET_ARG_VAL_LF;
                    pos++;
                } else {
                    goto redis_err;
                }
                break;
            case REQ_GET_ARG_VAL_LF:
                if (ch == LF) {
                    if (redis_data->curr_arg < 2)
                        redis_data->stage = REQ_GET_ARG_COMMAND_OR_KEY;
                    else
                        redis_data->stage = REQ_GET_ARG_VAL;
                    pos++;
                } else {
                    goto redis_err;
                }
                break;
            case REQ_GET_ARG_COMMAND_OR_KEY:
                if (ch == CR) {
                    redis_data->stage = REQ_GET_ARG_LEN_LF;
                    pos++;
                    break;
                }
                if (redis_data->curr_arg_len + pos > s->len) {
                    redis_data->key = wstrCatLen(redis_data->key,
                            (char *)&s->data[pos], s->len-pos);
                    redis_data->curr_arg_len -= (s->len-pos);
                    pos = s->len;
                    return pos;
                }
                redis_data->key = wstrCatLen(redis_data->key,
                        (char *)&s->data[pos], redis_data->curr_arg_len);
                pos += redis_data->curr_arg_len;
                redis_data->curr_arg++;
                redis_data->curr_arg_len = 0;
                if (redis_data->curr_arg == 1) {
                    // redis_data->key is stand for command
                    command_type = redisCommandHandle(redis_data,
                            redis_data->key);
                    if (command_type == REDIS_UNKNOWN)
                        goto redis_err;
                    redis_data->command_type = command_type;
                    if (command_type > REDIS_ZSCORE)
                        redis_data->is_read = 0;
                    else
                        redis_data->is_read = 1;

                    // Now duplicate `key` to command
                    redis_data->command = wstrDup(redis_data->key);
                    wstrClear(redis_data->key);
                } else {
                    // redis_data->key is stand for key
                    redis_data->key_end_pos = pos;
                }
                break;
            case REQ_GET_ARG_VAL:
                if (ch == CR) {
                    redis_data->stage = REQ_GET_ARG_LEN_LF;
                    pos++;
                    break;
                }
                if (redis_data->curr_arg_len + pos > s->len) {
                    redis_data->curr_arg_len -= (s->len-pos);
                    pos = s->len;
                    return pos;
                }
                pos += redis_data->curr_arg_len;
                redis_data->curr_arg++;
                redis_data->curr_arg_len = 0;
                break;
            case MES_END:
                return pos;
            default:
                goto redis_err;
        }
    }
    return pos;

redis_err:
    redis_data->stage = MES_ERR;
    return -1;
}

int parseRedis(struct conn *c, struct slice *slice, size_t *out)
{
    ssize_t nparsed;
    struct redisProcData *redis_data = c->protocol_data;

    if (isOuterClient(c->client)) {
        nparsed = redisReqParser(redis_data, slice);
    } else {
        nparsed = redisResParser(redis_data, slice);
    }

    if (nparsed == -1) {
        wstr info = wstrNewLen(slice->data, (int)slice->len);
        wheatLog(WHEAT_VERBOSE, "%d parseRedis() failed: %s", isOuterClient(c->client),
                info);
        wstrFree(info);
        return WHEAT_WRONG;
    }
    if (out) *out = nparsed;
    slice->len = nparsed;
    arrayPush(redis_data->req_body, slice);
    if (REDIS_FINISHED(redis_data)) {
        return WHEAT_OK;
    }
    return 1;
}

void getRedisKey(struct conn *c, struct slice *out)
{
    struct redisProcData *redis_data = c->protocol_data;
    sliceTo(out, (uint8_t *)redis_data->key, wstrlen(redis_data->key));
}

void getRedisCommand(struct conn *c, struct slice *out)
{
    struct redisProcData *redis_data = c->protocol_data;
    if (redis_data->command)
        sliceTo(out, (uint8_t *)redis_data->command, wstrlen(redis_data->command));
}

int getRedisArgs(struct conn *c)
{
    return ((struct redisProcData *)c->protocol_data)->args;
}

size_t getRedisKeyEndPos(struct conn *c)
{
    return ((struct redisProcData *)c->protocol_data)->key_end_pos;
}

void *initRedisData()
{
    struct redisProcData *data = wmalloc(sizeof(struct redisProcData));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    data->stage = MES_START;
    data->command_type = REDIS_UNKNOWN;
    data->command = NULL;
    data->req_body = arrayCreate(sizeof(struct slice), 4);
    data->key = wstrEmpty();
    data->pos = 0;
    data->key_end_pos = 0;
    return data;
}

void freeRedisData(void *d)
{
    struct redisProcData *data = d;
    if (data->command)
        wstrFree(data->command);
    wstrFree(data->key);
    arrayDealloc(data->req_body);
    wfree(d);
}

int initRedis()
{
    RedisHashTag = wstrEmpty();
    return WHEAT_OK;
}

void deallocRedis()
{
}

void redisBodyStart(struct conn *c)
{
    struct redisProcData *data = c->protocol_data;
    data->pos = 0;
}

struct slice *redisBodyNext(struct conn *c)
{
    struct redisProcData *data = c->protocol_data;
    if (data->pos < narray(data->req_body))
        return arrayIndex(data->req_body, data->pos++);
    return NULL;
}

int isReadCommand(struct conn *c)
{
    struct redisProcData *data = c->protocol_data;
    return data->is_read;
}

int redisSpot(struct conn *c)
{
    int ret;
    struct app **app_p, *app;

    app_p = arrayIndex(WorkerProcess->apps, 0);
    app = *app_p;
    if (!app->is_init) {
        ret = initApp(app);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
    }
    c->app = app;
    ret = initAppData(c);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "init app data failed");
        return WHEAT_WRONG;
    }
    ret = app->appCall(c, NULL);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "app failed, exited");
        app->deallocApp();
        app->is_init = 0;
    }
    return ret;
}
