#include "../protocol.h"
#include "proto_redis.h"

#define CR                  (uint8_t)13
#define LF                  (uint8_t)10

#define REDIS_FINISHED(r)   (((r)->stage) == MES_END)

enum redisCommand {
    REQ_REDIS_DEL,                    /* redis commands - keys */
    REQ_REDIS_EXISTS,
    REQ_REDIS_EXPIRE,
    REQ_REDIS_EXPIREAT,
    REQ_REDIS_PEXPIRE,
    REQ_REDIS_PEXPIREAT,
    REQ_REDIS_PERSIST,
    REQ_REDIS_PTTL,
    REQ_REDIS_TTL,
    REQ_REDIS_TYPE,
    REQ_REDIS_APPEND,                 /* redis requests - string */
    REQ_REDIS_BITCOUNT,
    REQ_REDIS_DECR,
    REQ_REDIS_DECRBY,
    REQ_REDIS_GET,
    REQ_REDIS_GETBIT,
    REQ_REDIS_GETRANGE,
    REQ_REDIS_GETSET,
    REQ_REDIS_INCR,
    REQ_REDIS_INCRBY,
    REQ_REDIS_INCRBYFLOAT,
    REQ_REDIS_MGET,
    REQ_REDIS_PSETEX,
    REQ_REDIS_SET,
    REQ_REDIS_SETBIT,
    REQ_REDIS_SETEX,
    REQ_REDIS_SETNX,
    REQ_REDIS_SETRANGE,
    REQ_REDIS_STRLEN,
    REQ_REDIS_HDEL,                   /* redis requests - hashes */
    REQ_REDIS_HEXISTS,
    REQ_REDIS_HGET,
    REQ_REDIS_HGETALL,
    REQ_REDIS_HINCRBY,
    REQ_REDIS_HINCRBYFLOAT,
    REQ_REDIS_HKEYS,
    REQ_REDIS_HLEN,
    REQ_REDIS_HMGET,
    REQ_REDIS_HMSET,
    REQ_REDIS_HSET,
    REQ_REDIS_HSETNX,
    REQ_REDIS_HVALS,
    REQ_REDIS_LINDEX,                 /* redis requests - lists */
    REQ_REDIS_LINSERT,
    REQ_REDIS_LLEN,
    REQ_REDIS_LPOP,
    REQ_REDIS_LPUSH,
    REQ_REDIS_LPUSHX,
    REQ_REDIS_LRANGE,
    REQ_REDIS_LREM,
    REQ_REDIS_LSET,
    REQ_REDIS_LTRIM,
    REQ_REDIS_RPOP,
    REQ_REDIS_RPUSH,
    REQ_REDIS_RPUSHX,
    REQ_REDIS_SADD,                   /* redis requests - sets */
    REQ_REDIS_SCARD,
    REQ_REDIS_SISMEMBER,
    REQ_REDIS_SMEMBERS,
    REQ_REDIS_SPOP,
    REQ_REDIS_SRANDMEMBER,
    REQ_REDIS_SREM,
    REQ_REDIS_ZADD,                   /* redis requests - sorted sets */
    REQ_REDIS_ZCARD,
    REQ_REDIS_ZCOUNT,
    REQ_REDIS_ZINCRBY,
    REQ_REDIS_ZRANGE,
    REQ_REDIS_ZRANGEBYSCORE,
    REQ_REDIS_ZRANK,
    REQ_REDIS_ZREM,
    REQ_REDIS_ZREMRANGEBYRANK,
    REQ_REDIS_ZREMRANGEBYSCORE,
    REQ_REDIS_ZREVRANGE,
    REQ_REDIS_ZREVRANGEBYSCORE,
    REQ_REDIS_ZREVRANK,
    REQ_REDIS_ZSCORE,
    REQ_REDIS_UNKNOWN
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

struct redisProcData {
    int curr_arg_len;
    int curr_arg;
    int args;
    wstr key;
//  struct slice *argvs;
    enum reqStage stage;
    enum redisCommand command_type;

    struct array *req_body;
    size_t pos;
};

static int redisCommandHandle(struct redisProcData *redis_data,
        wstr command)
{
    uint8_t *c = (uint8_t *)command;
    int args = wstrlen(command);
    switch (args) {
        case 3:
            if (str3icmp(c, 'd', 'e', 'l'))
                return REQ_REDIS_DEL;
            if (str3icmp(c, 'g', 'e', 't'))
                return REQ_REDIS_GET;
            if (str3icmp(c, 's', 'e', 't'))
                return REQ_REDIS_SET;
            if (str3icmp(c, 't', 't', 'l'))
                return REQ_REDIS_TTL;

            break;
        case 4:
            if (str4icmp(c, 'p', 't', 't', 'l'))
                return REQ_REDIS_PTTL;

            if (str4icmp(c, 'd', 'e', 'c', 'r'))
                return REQ_REDIS_DECR;

            if (str4icmp(c, 'h', 'd', 'e', 'l'))
                return REQ_REDIS_HDEL;

            if (str4icmp(c, 'h', 'g', 'e', 't'))
                return REQ_REDIS_HGET;

            if (str4icmp(c, 'h', 'l', 'e', 'n'))
                return REQ_REDIS_HLEN;

            if (str4icmp(c, 'h', 's', 'e', 't'))
                return REQ_REDIS_HSET;

            if (str4icmp(c, 'i', 'n', 'c', 'r'))
                return REQ_REDIS_INCR;

            if (str4icmp(c, 'l', 'l', 'e', 'n'))
                return REQ_REDIS_LLEN;

            if (str4icmp(c, 'l', 'p', 'o', 'p'))
                return REQ_REDIS_LPOP;

            if (str4icmp(c, 'l', 'r', 'e', 'm'))
                return REQ_REDIS_LREM;

            if (str4icmp(c, 'l', 's', 'e', 't'))
                return REQ_REDIS_LSET;

            if (str4icmp(c, 'r', 'p', 'o', 'p'))
                return REQ_REDIS_RPOP;

            if (str4icmp(c, 's', 'a', 'd', 'd'))
                return REQ_REDIS_SADD;

            if (str4icmp(c, 's', 'p', 'o', 'p'))
                return REQ_REDIS_SPOP;

            if (str4icmp(c, 's', 'r', 'e', 'm'))
                return REQ_REDIS_SREM;

            if (str4icmp(c, 't', 'y', 'p', 'e'))
                return REQ_REDIS_TYPE;

            if (str4icmp(c, 'm', 'g', 'e', 't'))
                return REQ_REDIS_MGET;

            if (str4icmp(c, 'z', 'a', 'd', 'd'))
                return REQ_REDIS_ZADD;

            if (str4icmp(c, 'z', 'r', 'e', 'm'))
                return REQ_REDIS_ZREM;

            break;

        case 5:
            if (str5icmp(c, 'h', 'k', 'e', 'y', 's'))
                return REQ_REDIS_HKEYS;

            if (str5icmp(c, 'h', 'm', 'g', 'e', 't'))
                return REQ_REDIS_HMGET;

            if (str5icmp(c, 'h', 'm', 's', 'e', 't'))
                return REQ_REDIS_HMSET;

            if (str5icmp(c, 'h', 'v', 'a', 'l', 's'))
                return REQ_REDIS_HVALS;

            if (str5icmp(c, 'l', 'p', 'u', 's', 'h'))
                return REQ_REDIS_LPUSH;

            if (str5icmp(c, 'l', 't', 'r', 'i', 'm'))
                return REQ_REDIS_LTRIM;

            if (str5icmp(c, 'r', 'p', 'u', 's', 'h'))
                return REQ_REDIS_RPUSH;

            if (str5icmp(c, 's', 'c', 'a', 'r', 'd'))
                return REQ_REDIS_SCARD;

            if (str5icmp(c, 's', 'e', 't', 'e', 'x'))
                return REQ_REDIS_SETEX;

            if (str5icmp(c, 's', 'e', 't', 'n', 'x'))
                return REQ_REDIS_SETNX;

            if (str5icmp(c, 'z', 'c', 'a', 'r', 'd'))
                return REQ_REDIS_ZCARD;

            if (str5icmp(c, 'z', 'r', 'a', 'n', 'k'))
                return REQ_REDIS_ZRANK;

            break;

        case 6:
            if (str6icmp(c, 'a', 'p', 'p', 'e', 'n', 'd'))
                return REQ_REDIS_APPEND;

            if (str6icmp(c, 'd', 'e', 'c', 'r', 'b', 'y'))
                return REQ_REDIS_DECRBY;

            if (str6icmp(c, 'e', 'x', 'i', 's', 't', 's'))
                return REQ_REDIS_EXISTS;

            if (str6icmp(c, 'e', 'x', 'p', 'i', 'r', 'e'))
                return REQ_REDIS_EXPIRE;

            if (str6icmp(c, 'g', 'e', 't', 'b', 'i', 't'))
                return REQ_REDIS_GETBIT;

            if (str6icmp(c, 'g', 'e', 't', 's', 'e', 't'))
                return REQ_REDIS_GETSET;

            if (str6icmp(c, 'p', 's', 'e', 't', 'e', 'x'))
                return REQ_REDIS_PSETEX;

            if (str6icmp(c, 'h', 's', 'e', 't', 'n', 'x'))
                return REQ_REDIS_HSETNX;

            if (str6icmp(c, 'i', 'n', 'c', 'r', 'b', 'y'))
                return REQ_REDIS_INCRBY;

            if (str6icmp(c, 'l', 'i', 'n', 'd', 'e', 'x'))
                return REQ_REDIS_LINDEX;

            if (str6icmp(c, 'l', 'p', 'u', 's', 'h', 'x'))
                return REQ_REDIS_LPUSHX;

            if (str6icmp(c, 'l', 'r', 'a', 'n', 'g', 'e'))
                return REQ_REDIS_LRANGE;

            if (str6icmp(c, 'r', 'p', 'u', 's', 'h', 'x'))
                return REQ_REDIS_RPUSHX;

            if (str6icmp(c, 's', 'e', 't', 'b', 'i', 't'))
                return REQ_REDIS_SETBIT;

            if (str6icmp(c, 's', 't', 'r', 'l', 'e', 'n'))
                return REQ_REDIS_STRLEN;

            if (str6icmp(c, 'z', 'c', 'o', 'u', 'n', 't'))
                return REQ_REDIS_ZCOUNT;

            if (str6icmp(c, 'z', 'r', 'a', 'n', 'g', 'e'))
                return REQ_REDIS_ZRANGE;

            if (str6icmp(c, 'z', 's', 'c', 'o', 'r', 'e'))
                return REQ_REDIS_ZSCORE;

            break;

        case 7:
            if (str7icmp(c, 'p', 'e', 'r', 's', 'i', 's', 't'))
                return REQ_REDIS_PERSIST;

            if (str7icmp(c, 'p', 'e', 'x', 'p', 'i', 'r', 'e'))
                return REQ_REDIS_PEXPIRE;

            if (str7icmp(c, 'h', 'e', 'x', 'i', 's', 't', 's'))
                return REQ_REDIS_HEXISTS;

            if (str7icmp(c, 'h', 'g', 'e', 't', 'a', 'l', 'l'))
                return REQ_REDIS_HGETALL;

            if (str7icmp(c, 'h', 'i', 'n', 'c', 'r', 'b', 'y'))
                return REQ_REDIS_HINCRBY;

            if (str7icmp(c, 'l', 'i', 'n', 's', 'e', 'r', 't'))
                return REQ_REDIS_LINSERT;

            if (str7icmp(c, 'z', 'i', 'n', 'c', 'r', 'b', 'y'))
                return REQ_REDIS_ZINCRBY;

            break;

        case 8:
            if (str8icmp(c, 'e', 'x', 'p', 'i', 'r', 'e', 'a', 't'))
                return REQ_REDIS_EXPIREAT;

            if (str8icmp(c, 'b', 'i', 't', 'c', 'o', 'u', 'n', 't'))
                return REQ_REDIS_BITCOUNT;

            if (str8icmp(c, 'g', 'e', 't', 'r', 'a', 'n', 'g', 'e'))
                return REQ_REDIS_GETRANGE;

            if (str8icmp(c, 's', 'e', 't', 'r', 'a', 'n', 'g', 'e'))
                return REQ_REDIS_SETRANGE;

            if (str8icmp(c, 's', 'm', 'e', 'm', 'b', 'e', 'r', 's'))
                return REQ_REDIS_SMEMBERS;

            if (str8icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'k'))
                return REQ_REDIS_ZREVRANK;

            break;

        case 9:
            if (str9icmp(c, 'p', 'e', 'x', 'p', 'i', 'r', 'e', 'a', 't'))
                return REQ_REDIS_PEXPIREAT;

            if (str9icmp(c, 's', 'i', 's', 'm', 'e', 'm', 'b', 'e', 'r'))
                return REQ_REDIS_SISMEMBER;

            if (str9icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'g', 'e'))
                return REQ_REDIS_ZREVRANGE;

            break;

        case 11:
            if (str11icmp(c, 'i', 'n', 'c', 'r', 'b', 'y', 'f', 'l', 'o', 'a', 't'))
                return REQ_REDIS_INCRBYFLOAT;

            if (str11icmp(c, 's', 'r', 'a', 'n', 'd', 'm', 'e', 'm', 'b', 'e', 'r'))
                return REQ_REDIS_SRANDMEMBER;

            break;

        case 12:
            if (str12icmp(c, 'h', 'i', 'n', 'c', 'r', 'b', 'y', 'f', 'l', 'o', 'a', 't'))
                return REQ_REDIS_HINCRBYFLOAT;

            break;

        case 13:
            if (str13icmp(c, 'z', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REQ_REDIS_ZRANGEBYSCORE;

            break;

        case 15:
            if (str15icmp(c, 'z', 'r', 'e', 'm', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 'r', 'a', 'n', 'k'))
                return REQ_REDIS_ZREMRANGEBYRANK;

            break;

        case 16:
            if (str16icmp(c, 'z', 'r', 'e', 'm', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REQ_REDIS_ZREMRANGEBYSCORE;

            if (str16icmp(c, 'z', 'r', 'e', 'v', 'r', 'a', 'n', 'g', 'e', 'b', 'y', 's', 'c', 'o', 'r', 'e'))
                return REQ_REDIS_ZREVRANGEBYSCORE;

            break;
    }
    return REQ_REDIS_UNKNOWN;
}

static ssize_t redisResParser(struct redisProcData *redis_data, struct slice *s)
{
    size_t pos = 0;
    while (pos < s->len) {
        char ch = s->data[pos];
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
                if (ch == CR)
                    redis_data->stage = RES_NIL_VAL_LF;
                else
                    goto redis_err;
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
            case RES_NIL_VAL_LF:
                if (ch == LF)
                    redis_data->stage = RES_GET_ARG_LEN;
                else
                    goto redis_err;
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
    size_t pos = 0;
    enum redisCommand command_type;
    while (pos < s->len) {
        char ch = s->data[pos];
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
                    if (redis_data->args < 2)
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
                    command_type = redisCommandHandle(redis_data,
                            redis_data->key);
                    if (command_type == REQ_REDIS_UNKNOWN)
                        goto redis_err;
                    redis_data->command_type = command_type;
                    wstrClear(redis_data->key);
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
        wstr info = wstrNewLen(slice->data, slice->len);
        wheatLog(WHEAT_VERBOSE, "%d parseRedis() failedd: %s", isOuterClient(c->client),
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

int getRedisKey(struct conn *c, struct slice *out)
{
    struct redisProcData *redis_data = c->protocol_data;
    if (redis_data->args < 2)
        return WHEAT_WRONG;
    sliceTo(out, (uint8_t *)redis_data->key, wstrlen(redis_data->key));
    return WHEAT_OK;
}

void *initRedisData()
{
    struct redisProcData *data = wmalloc(sizeof(struct redisProcData));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    data->stage = MES_START;
    data->command_type = REQ_REDIS_UNKNOWN;
    data->req_body = arrayCreate(sizeof(struct slice), 4);
    data->pos = 0;
    data->key = wstrEmpty();
    return data;
}

void freeRedisData(void *d)
{
    struct redisProcData *data = d;
    wstrFree(data->key);
    arrayDealloc(data->req_body);
    wfree(d);
}

int initRedis()
{
    return WHEAT_OK;
}

void deallocRedis()
{
}

struct slice *redisBodyNext(struct conn *c)
{
    struct redisProcData *data = c->protocol_data;
    if (data->pos < narray(data->req_body))
        return arrayIndex(data->req_body, data->pos++);
    return NULL;
}

int redisSpot(struct conn *c)
{
    int i = 2, ret;
    if (!AppTable[i].is_init) {
        ret = AppTable[i].initApp(c->client->protocol);
        if (ret == WHEAT_WRONG)
            return WHEAT_WRONG;
        AppTable[i].is_init = 1;
    }
    c->app = &AppTable[i];
    ret = initAppData(c);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "init app data failed");
        return WHEAT_WRONG;
    }
    ret = AppTable[i].appCall(c, NULL);
    return ret;
}
