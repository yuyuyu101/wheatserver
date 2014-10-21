// Ramcloud handler module
//
// Copyright (c) 2014 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "../application.h"
#include "../../protocol/memcache/proto_memcache.h"
#include "CRamCloud.h"

#define RAMCLOUD_DEFAULT_VALUE_LEN 1024 * 64
#define VALUE "VALUE"
#define END "END"
#define CRLF "\r\n"
#define FLAG_SIZE sizeof(uint16_t)

#define STORED    "STORED\r\n"
#define NOT_STORED    "NOT_STORED\r\n"
#define EXISTS    "EXISTS\r\n"
#define NOT_FOUND    "NOT_FOUND\r\n"
#define DELETED    "DELETED\r\n"
#define SERVER_ERROR    "SERVER_ERROR BUG\r\n"
#define CLIENT_ERROR    "CLIENT_ERROR INVALID ARGUMENT\r\n"

int callRamcloud(struct conn *, void *);
int initRamcloud(struct protocol *);
void deallocRamcloud();
void *initRamcloudData(struct conn *);
void freeRamcloudData(void *app_data);

// Static File Configuration
static struct configuration RamcloudConf[] = {
    {"ramcloud-cluster-name",    2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"ramcloud-server-locator",  2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
};


static struct app AppRamcloud = {
    "Memcache", NULL, callRamcloud, initRamcloud,
    deallocRamcloud, initRamcloudData, freeRamcloudData, 0
};

struct moduleAttr AppRamcloudAttr = {
    "ramcloud", APP, {.app=&AppRamcloud}, NULL, 0,
    RamcloudConf, sizeof(RamcloudConf)/sizeof(struct configuration),
    NULL, 0
};

uint64_t table_id = 1234;

struct ramcloudGlobal {
    struct rc_client *client;
} global;

struct ramcloudData {
    struct array *retrievals;
    struct array *retrievals_keys;
    struct array *retrievals_vals;
    struct array *retrievals_flags;
    struct array *retrievals_versions;
    struct slice storage_response;
    wstr retrieval_response;
    uint64_t retrieval_len;
};

static void buildRetrievalResponse(struct ramcloudData *d, int cas)
{
    int i = 0;
    char buf[64];
    ASSERT(!d->retrieval_response);
    // Estimate value avoid realloc
    d->retrieval_response = wstrNewLen(NULL, d->retrieval_len+narray(d->retrievals_vals)*64);
    for (; i < narray(d->retrievals_vals); ++i) {
        struct slice *val = arrayIndex(d->retrievals_vals, i);
        struct slice *key = arrayIndex(d->retrievals_keys, i);
        uint16_t *flag = arrayIndex(d->retrievals_flags, i);
        uint64_t *v = arrayIndex(d->retrievals_versions, i);
        d->retrieval_response = wstrCatLen(d->retrieval_response, VALUE, 5);
        d->retrieval_response = wstrCatLen(d->retrieval_response, " ", 1);
        d->retrieval_response = wstrCatLen(d->retrieval_response, (char*)key->data, key->len);
        d->retrieval_response = wstrCatLen(d->retrieval_response, " ", 1);
        int l = sprintf(buf, "%d", *flag);
        d->retrieval_response = wstrCatLen(d->retrieval_response, buf, l);
        d->retrieval_response = wstrCatLen(d->retrieval_response, " ", 1);
        l = sprintf(buf, "%ld", val->len);
        d->retrieval_response = wstrCatLen(d->retrieval_response, buf, l);
        if (cas) {
            d->retrieval_response = wstrCatLen(d->retrieval_response, " ", 1);
            l = sprintf(buf, "%llu", *v);
            d->retrieval_response = wstrCatLen(d->retrieval_response, buf, l);
        }
        d->retrieval_response = wstrCatLen(d->retrieval_response, CRLF, 2);
        d->retrieval_response = wstrCatLen(d->retrieval_response, (char*)val->data, val->len);
        d->retrieval_response = wstrCatLen(d->retrieval_response, CRLF, 2);
    }

    d->retrieval_response = wstrCatLen(d->retrieval_response, END, 3);
    d->retrieval_response = wstrCatLen(d->retrieval_response, CRLF, 2);
}

static void ramcloudRead(void *item, void *data)
{
    wstr i = *(wstr*)item;
    struct ramcloudData *d = data;
    uint32_t actual_len;
    uint64_t version;
    uint64_t len = RAMCLOUD_DEFAULT_VALUE_LEN;

    wheatLog(WHEAT_DEBUG, "%s read key %s", __func__, i);
    wstr val = wstrNewLen(NULL, len);
    Status s = rc_read(global.client, table_id, i, wstrlen(i), NULL, &version, val, len, &actual_len);
    if (s != STATUS_OK) {
        if (s != STATUS_OBJECT_DOESNT_EXIST) {
            wheatLog(WHEAT_WARNING, " failed to read %s: %s", i, statusToString(s));
        }
        wstrFree(val);
        return ;
    }

    while (actual_len > len) {
        wstrFree(val);
        len = actual_len;
        val = wstrNewLen(NULL, actual_len);
        if (val == NULL) {
            wheatLog(WHEAT_WARNING, " failed to alloc memory");
            return ;
        }

        s = rc_read(global.client, table_id, i, wstrlen(i), NULL, &version, val, len, &actual_len);
        if (s != STATUS_OK) {
            if (s != STATUS_OBJECT_DOESNT_EXIST) {
                wheatLog(WHEAT_WARNING, " failed to read %s: %s", i, statusToString(s));
            }
            wstrFree(val);
            return ;
        }
    }

    d->retrieval_len += actual_len;
    arrayPush(d->retrievals, &val);
    struct slice ss = {(uint8_t*)(i), wstrlen(i)};
    arrayPush(d->retrievals_keys, &ss);
    ss.data = (uint8_t*)val;
    ss.len = actual_len - FLAG_SIZE;
    arrayPush(d->retrievals_vals, &ss);
    arrayPush(d->retrievals_flags, &val[actual_len-FLAG_SIZE]);
    arrayPush(d->retrievals_versions, &version);
}

static void ramcloudDelete(struct ramcloudData* d, wstr key, struct RejectRules *rule)
{
    wheatLog(WHEAT_DEBUG, "%s delete key %s", __func__, key);
    uint64_t version;
    Status s = rc_remove(global.client, table_id, key, wstrlen(key), rule, &version);
    if (s != STATUS_OK) {
        if (s == STATUS_OBJECT_DOESNT_EXIST) {
            sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
        } else {
            wheatLog(WHEAT_WARNING, "%s failed to remove %s: %s", __func__, key, statusToString(s));
            sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        }
    } else {
        sliceTo(&d->storage_response, (uint8_t*)DELETED, sizeof(DELETED)-1);
    }
}

static void ramcloudSet(struct ramcloudData *d, wstr key, struct array *vals, uint32_t vlen, uint16_t flag, struct RejectRules *rule)
{
    wheatLog(WHEAT_DEBUG, "%s set key %s", __func__, key);
    uint64_t version;
    int i = 0;
    struct slice *slice;
    wstr val = wstrNewLen(NULL, vlen+sizeof(flag));
    while (i < narray(vals)) {
        slice = arrayIndex(vals, i);
        val = wstrCatLen(val, (char*)slice->data, slice->len);
        ++i;
    }
    val = wstrCatLen(val, (char*)&flag, FLAG_SIZE);

    Status s = rc_write(global.client, table_id, key, wstrlen(key), val, wstrlen(val),
                        rule, &version);
    wstrFree(val);
    if (s != STATUS_OK) {
        // cas command
        if (rule->versionNeGiven) {
            if (s == STATUS_WRONG_VERSION)
                sliceTo(&d->storage_response, (uint8_t*)EXISTS, sizeof(EXISTS)-1);
            else if (s == STATUS_OBJECT_DOESNT_EXIST)
                sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
        } else if ((rule->doesntExist && s == STATUS_OBJECT_DOESNT_EXIST) ||
                   (rule->exists && s == STATUS_OBJECT_EXISTS)) {
            sliceTo(&d->storage_response, (uint8_t*)NOT_STORED, sizeof(NOT_STORED)-1);
        } else {
            wheatLog(WHEAT_WARNING, "%s failed to set %s: %s", __func__, key, statusToString(s));
            sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        }
    } else {
        sliceTo(&d->storage_response, (uint8_t*)STORED, sizeof(STORED)-1);
    }
}

static void ramcloudIncr(struct ramcloudData *d, wstr key, uint64_t num, int positive)
{
    if (num > INT64_MAX) {
        wheatLog(WHEAT_WARNING, "%s num %ld can't larger than %ld", __func__, num, INT64_MAX);
        sliceTo(&d->storage_response, (uint8_t*)CLIENT_ERROR, sizeof(CLIENT_ERROR)-1);
        return ;
    }

    static int max_len = 100;
    char value[max_len];
    uint32_t actual_len;
    uint64_t version;
    uint16_t flag;

again:
    wheatLog(WHEAT_DEBUG, "%s incr key %s %ld", __func__, key, num);
    Status s = rc_read(global.client, table_id, key, wstrlen(key), NULL, &version, value, max_len, &actual_len);
    if (s != STATUS_OK) {
        if (s != STATUS_OBJECT_DOESNT_EXIST) {
            wheatLog(WHEAT_WARNING, " failed to read %s: %s", key, statusToString(s));
            sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        } else {
            sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
        }
        return ;
    }

    if (actual_len > max_len) {
        sliceTo(&d->storage_response, (uint8_t*)CLIENT_ERROR, sizeof(CLIENT_ERROR)-1);
        return ;
    }

    flag = *(uint16_t*)&value[actual_len-FLAG_SIZE];
    value[actual_len-FLAG_SIZE] = '\0';
    long long n = atoll(value);
    if (n == 0) {
        sliceTo(&d->storage_response, (uint8_t*)CLIENT_ERROR, sizeof(CLIENT_ERROR)-1);
        return ;
    }
    if (positive) {
        n += num;
    } else {
        if (num > n)
            n = 0;
        else
            n -= num;
    }
    int l = sprintf(value, "%llu", n);
    memcpy(&value[l], &flag, FLAG_SIZE);
    struct RejectRules rule;
    memset(&rule, 0, sizeof(rule));
    rule.doesntExist = 1;
    rule.versionNeGiven = 1;
    rule.givenVersion = version;
    s = rc_write(global.client, table_id, key, wstrlen(key), value, l+FLAG_SIZE,
                 &rule, &version);
    if (s != STATUS_OK) {
        if (s == STATUS_WRONG_VERSION) {
            goto again;
        }

        if (s == STATUS_OBJECT_DOESNT_EXIST) {
            sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
            return ;
        }
        sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        wheatLog(WHEAT_WARNING, "%s failed to incr %s: %s", __func__, key, statusToString(s));
        return ;
    } else {
        l = sprintf(value, "%llu\r\n", n);
        d->retrieval_response = wstrNewLen(value, l);
        sliceTo(&d->storage_response, (uint8_t*)d->retrieval_response, wstrlen(d->retrieval_response));
    }
}

static void ramcloudAppend(struct ramcloudData *d, wstr key, struct array *vals, uint32_t vlen, uint16_t flag, int append)
{
    uint32_t actual_len;
    uint64_t version;
    uint64_t len = RAMCLOUD_DEFAULT_VALUE_LEN;

again:
    wheatLog(WHEAT_DEBUG, "%s read key %s", __func__, key);

    wstr origin_val = wstrNewLen(NULL, len);
    wstr val;
    Status s = rc_read(global.client, table_id, key, wstrlen(key), NULL, &version, origin_val, len, &actual_len);
    if (s != STATUS_OK) {
        if (s != STATUS_OBJECT_DOESNT_EXIST) {
            wheatLog(WHEAT_WARNING, " failed to read %s: %s", key, statusToString(s));
            sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        } else {
            sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
        }
        wstrFree(origin_val);
        return ;
    }

    while (actual_len > len) {
        wstrFree(origin_val);
        len = actual_len;
        origin_val = wstrNewLen(NULL, actual_len);
        if (origin_val == NULL) {
            sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
            wheatLog(WHEAT_WARNING, " failed to alloc memory");
            return ;
        }

        s = rc_read(global.client, table_id, key, wstrlen(key), NULL, &version, origin_val, len, &actual_len);
        if (s != STATUS_OK) {
            if (s != STATUS_OBJECT_DOESNT_EXIST) {
                wheatLog(WHEAT_WARNING, " failed to read %s: %s", key, statusToString(s));
                sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
            } else {
                sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
            }
            wstrFree(origin_val);
            return ;
        }
    }

    wstrupdatelen(origin_val, actual_len-FLAG_SIZE);
    if (append) {
        val = origin_val;
        // reduce flag size
    } else {
        val = wstrNewLen(NULL, vlen);
    }

    int i = 0;
    while (i < narray(vals)) {
        struct slice *slice;
        slice = arrayIndex(vals, i);
        val = wstrCatLen(val, (char*)slice->data, slice->len);
        len += slice->len;
        ++i;
    }

    if (!append) {
        val = wstrCatLen(val, origin_val, wstrlen(origin_val));
        wstrFree(origin_val);
    }
    val = wstrCatLen(val, (char*)&flag, FLAG_SIZE);

    struct RejectRules rule;
    memset(&rule, 0, sizeof(rule));
    rule.doesntExist = 1;
    rule.versionNeGiven = 1;
    rule.givenVersion = version;
    s = rc_write(global.client, table_id, key, wstrlen(key), val, wstrlen(val),
                        &rule, NULL);
    wstrFree(val);
    if (s != STATUS_OK) {
        if (s == STATUS_OBJECT_DOESNT_EXIST) {
            sliceTo(&d->storage_response, (uint8_t*)NOT_FOUND, sizeof(NOT_FOUND)-1);
            return ;
        } else if (s == STATUS_WRONG_VERSION) {
            goto again;
        }

        wheatLog(WHEAT_WARNING, " failed to write %s: %s", key, statusToString(s));
        sliceTo(&d->storage_response, (uint8_t*)SERVER_ERROR, sizeof(SERVER_ERROR)-1);
        return ;
    }
    sliceTo(&d->storage_response, (uint8_t*)STORED, sizeof(STORED)-1);
}

int callRamcloud(struct conn *c, void *arg)
{
    enum memcacheCommand command = getMemcacheCommand(c->protocol_data);
    struct RejectRules rule;
    struct ramcloudData *d = c->app_private_data;
    memset(&rule, 0, sizeof(rule));
    switch (command) {
        case REQ_MC_GET:
        case REQ_MC_GETS:
            d->retrievals = arrayCreate(sizeof(wstr), 1);
            d->retrievals_keys = arrayCreate(sizeof(struct slice), 1);
            d->retrievals_vals = arrayCreate(sizeof(struct slice), 1);
            d->retrievals_flags = arrayCreate(FLAG_SIZE, 1);
            d->retrievals_versions = arrayCreate(sizeof(uint64_t), 1);
            arrayEach2(getMemcacheKeys(c->protocol_data), ramcloudRead, d);
            buildRetrievalResponse(d, command == REQ_MC_GETS ? 1 : 0);
            sliceTo(&d->storage_response, (uint8_t*)d->retrieval_response, wstrlen(d->retrieval_response));
            break;

        case REQ_MC_DELETE:
            rule.doesntExist = 1;
            ramcloudDelete(d, getMemcacheKey(c->protocol_data), &rule);
            break;

        case REQ_MC_ADD:
            rule.exists = 1;
            ramcloudSet(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                        getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), &rule);
            break;

        case REQ_MC_REPLACE:
            rule.doesntExist = 1;
            ramcloudSet(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                        getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), &rule);
            break;

        case REQ_MC_CAS:
            rule.givenVersion = getMemcacheCas(c->protocol_data);
            rule.versionNeGiven = 1;
            rule.doesntExist = 1;
            ramcloudSet(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                        getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), &rule);
            break;

        case REQ_MC_SET:
            ramcloudSet(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                        getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), &rule);
            break;

        case REQ_MC_INCR:
            ramcloudIncr(d, getMemcacheKey(c->protocol_data), getMemcacheNum(c->protocol_data), 1);
            break;

        case REQ_MC_DECR:
            ramcloudIncr(d, getMemcacheKey(c->protocol_data), getMemcacheNum(c->protocol_data), 0);
            break;

        case REQ_MC_APPEND:
            ramcloudAppend(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                           getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), 1);
            break;

        case REQ_MC_PREPEND:
            ramcloudAppend(d, getMemcacheKey(c->protocol_data), getMemcacheVal(c->protocol_data),
                           getMemcacheValLen(c->protocol_data), getMemcacheFlag(c->protocol_data), 0);
            break;

        default:
            ASSERT(0);
    }

    sendMemcacheResponse(c, &d->storage_response);

    return WHEAT_OK;
}

int initRamcloud(struct protocol *p)
{
    char *locator;
    char *cluster_name;
    struct configuration *conf;
    conf = getConfiguration("ramcloud-server-locator");
    locator = conf->target.ptr;
    conf = getConfiguration("ramcloud-cluster-name");
    cluster_name = conf->target.ptr;
    if (!locator || !cluster_name) {
        wheatLog(WHEAT_WARNING, "%s lack of necessary config", __func__);
        goto err;
    }

    Status s = rc_connect(locator, cluster_name, &global.client);
    if (s != STATUS_OK) {
        wheatLog(WHEAT_WARNING, "%s connect to %s: %s", __func__, locator, statusToString(s));
        goto err;
    }

    return WHEAT_OK;

 err:
    return WHEAT_WRONG;
}

void deallocRamcloud()
{
    rc_disconnect(global.client);
}

void *initRamcloudData(struct conn *c)
{
    struct ramcloudData *data = wmalloc(sizeof(struct ramcloudData));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    return data;
}

static void freeValue(void *k)
{
    wstrFree(*(wstr*)k);
}

void freeRamcloudData(void *data)
{
    struct ramcloudData *d = data;
    if (d->retrievals) {
        arrayEach(d->retrievals, freeValue);
        arrayDealloc(d->retrievals);
    }
    if (d->retrievals_keys)
        arrayDealloc(d->retrievals_keys);
    if (d->retrievals_vals)
        arrayDealloc(d->retrievals_vals);
    if (d->retrievals_flags)
        arrayDealloc(d->retrievals_flags);
    if (d->retrievals_versions)
        arrayDealloc(d->retrievals_versions);
    if (d->retrieval_response)
        wstrFree(d->retrieval_response);
    wfree(d);
}
