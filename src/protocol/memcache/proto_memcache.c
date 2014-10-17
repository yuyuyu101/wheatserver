// Memcache protocol parse implemetation
//
// Copyright (c) 2014 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include "../protocol.h"
#include "../str_macro.h"
#include "proto_memcache.h"

/*
 * From memcache protocol specification:
 *
 * Data stored by memcached is identified with the help of a key. A key
 * is a text string which should uniquely identify the data for clients
 * that are interested in storing and retrieving it.  Currently the
 * length limit of a key is set at 250 characters (of course, normally
 * clients wouldn't need to use such long keys); the key must not include
 * control characters or whitespace.
 */
#define MEMCACHE_MAX_KEY_LENGTH 250
#define MEMCACHE_FINISHED(r)   (((r)->stage) == SW_ALMOST_DONE)

#define CR                  (uint8_t)13
#define LF                  (uint8_t)10

int spotMemcache(struct conn *c);
int parseMemcache(struct conn *c, struct slice *slice, size_t *);
void *initMemcacheData();
void freeMemcacheData(void *d);
int initMemcache();
void deallocMemcache();

static struct protocol ProtocolMemcache = {
    spotMemcache, parseMemcache, initMemcacheData, freeMemcacheData,
        initMemcache, deallocMemcache,
};

struct moduleAttr ProtocolMemcacheAttr = {
    "Memcache", PROTOCOL, {.protocol=&ProtocolMemcache}, NULL, 0, NULL, 0, NULL, 0
};

enum reqStage {
    SW_START,
    SW_REQ_TYPE,
    SW_SPACES_BEFORE_KEY,
    SW_KEY,
    SW_SPACES_BEFORE_KEYS,
    SW_SPACES_BEFORE_FLAGS,
    SW_FLAGS,
    SW_SPACES_BEFORE_EXPIRY,
    SW_EXPIRY,
    SW_SPACES_BEFORE_VLEN,
    SW_VLEN,
    SW_SPACES_BEFORE_CAS,
    SW_CAS,
    SW_RUNTO_VAL,
    SW_VAL,
    SW_SPACES_BEFORE_NUM,
    SW_NUM,
    SW_RUNTO_CRLF,
    SW_CRLF,
    SW_NOREPLY,
    SW_AFTER_NOREPLY,
    SW_ALMOST_DONE,
    SW_SENTINEL
};

struct memcacheProcData {
    ssize_t token_pos;
    wstr command;
    wstr key;
    uint16_t flag;
    time_t expire;
    uint32_t vlen;
    uint32_t vlen_left;
    uint64_t cas;
    uint64_t num;
    struct array *keys;
    struct array *vals;
    enum reqStage stage;
    enum memcacheCommand type;
    wstr noreply_banner;
    int noreply;
    int quit;
};

/*
 * Return true, if the memcache command is a storage command, otherwise
 * return false
 */
static int memcache_storage(struct memcacheProcData *r)
{
    switch (r->type) {
    case REQ_MC_SET:
    case REQ_MC_CAS:
    case REQ_MC_ADD:
    case REQ_MC_REPLACE:
    case REQ_MC_APPEND:
    case REQ_MC_PREPEND:
        return 1;

    default:
        break;
    }

    return 0;
}

/*
 * Return true, if the memcache command is a cas command, otherwise
 * return false
 */
static int memcache_cas(struct memcacheProcData *r)
{
    if (r->type == REQ_MC_CAS) {
        return 1;
    }

    return 0;
}

/*
 * Return true, if the memcache command is a retrieval command, otherwise
 * return false
 */
static int memcache_retrieval(struct memcacheProcData *r)
{
    switch (r->type) {
    case REQ_MC_GET:
    case REQ_MC_GETS:
        return 1;

    default:
        break;
    }

    return 0;
}

/*
 * Return true, if the memcache command is a arithmetic command, otherwise
 * return false
 */
static int memcache_arithmetic(struct memcacheProcData *r)
{
    switch (r->type) {
    case REQ_MC_INCR:
    case REQ_MC_DECR:
        return 1;

    default:
        break;
    }

    return 0;
}

/*
 * Return true, if the memcache command is a delete command, otherwise
 * return false
 */
static int memcache_delete(struct memcacheProcData *r)
{
    if (r->type == REQ_MC_DELETE) {
        return 1;
    }

    return 0;
}


static ssize_t memcacheParseReq(struct memcacheProcData *r, struct slice *s)
{
    char *m;
    char ch;
    size_t command_len;

    size_t pos = 0;
    uint64_t left = 0;
    struct slice val_s;
    enum reqStage state = r->stage;
    r->token_pos = 0;
    while (pos < s->len) {
        ch = s->data[pos];

        switch (state) {
        case SW_START:
            if (!islower(ch)) {
                goto error;
            }

            r->token_pos = pos;
            state = SW_REQ_TYPE;
            break;

        case SW_REQ_TYPE:
            if (ch == ' ' || ch == CR) {
                r->type = UNKNOWN;
                if (wstrlen(r->command)) {
                    r->command = wstrCatLen(r->command, (char *)&s->data[r->token_pos], pos);
                    command_len = wstrlen(r->command);
                    m = (char*)&r->command;
                } else {
                    command_len = pos - r->token_pos;
                    m = (char*)&s->data[r->token_pos];
                }

                switch (command_len) {

                case 3:
                    if (str4icmp(m, 'g', 'e', 't', ' ')) {
                        r->type = REQ_MC_GET;
                        break;
                    }

                    if (str4icmp(m, 's', 'e', 't', ' ')) {
                        r->type = REQ_MC_SET;
                        break;
                    }

                    if (str4icmp(m, 'a', 'd', 'd', ' ')) {
                        r->type = REQ_MC_ADD;
                        break;
                    }

                    if (str4icmp(m, 'c', 'a', 's', ' ')) {
                        r->type = REQ_MC_CAS;
                        break;
                    }

                    break;

                case 4:
                    if (str4icmp(m, 'g', 'e', 't', 's')) {
                        r->type = REQ_MC_GETS;
                        break;
                    }

                    if (str4icmp(m, 'i', 'n', 'c', 'r')) {
                        r->type = REQ_MC_INCR;
                        break;
                    }

                    if (str4icmp(m, 'd', 'e', 'c', 'r')) {
                        r->type = REQ_MC_DECR;
                        break;
                    }

                    if (str4icmp(m, 'q', 'u', 'i', 't')) {
                        r->type = REQ_MC_QUIT;
                        r->quit = 1;
                        break;
                    }

                    break;

                case 6:
                    if (str6icmp(m, 'a', 'p', 'p', 'e', 'n', 'd')) {
                        r->type = REQ_MC_APPEND;
                        break;
                    }

                    if (str6icmp(m, 'd', 'e', 'l', 'e', 't', 'e')) {
                        r->type = REQ_MC_DELETE;
                        break;
                    }

                    break;

                case 7:
                    if (str7icmp(m, 'p', 'r', 'e', 'p', 'e', 'n', 'd')) {
                        r->type = REQ_MC_PREPEND;
                        break;
                    }

                    if (str7icmp(m, 'r', 'e', 'p', 'l', 'a', 'c', 'e')) {
                        r->type = REQ_MC_REPLACE;
                        break;
                    }

                    break;
                }

                switch (r->type) {
                case REQ_MC_GET:
                case REQ_MC_GETS:
                case REQ_MC_DELETE:
                case REQ_MC_CAS:
                case REQ_MC_SET:
                case REQ_MC_ADD:
                case REQ_MC_REPLACE:
                case REQ_MC_APPEND:
                case REQ_MC_PREPEND:
                case REQ_MC_INCR:
                case REQ_MC_DECR:
                    if (ch == CR) {
                        goto error;
                    }
                    state = SW_SPACES_BEFORE_KEY;
                    break;

                case REQ_MC_QUIT:
                    pos--; /* go back by 1 byte */
                    state = SW_CRLF;
                    break;

                case UNKNOWN:
                    goto error;

                default:
                    ASSERT(0);
                }
            } else if (!islower(ch)) {
                goto error;
            }

            break;

        case SW_SPACES_BEFORE_KEY:
            if (ch != ' ') {
                pos--; /* go back by 1 byte */
                r->token_pos = -1;
                state = SW_KEY;
            }
            break;

        case SW_KEY:
            if (r->token_pos == -1) {
                r->token_pos = pos;
            }
            if (ch == ' ' || ch == CR) {
                if (wstrlen(r->key) + (pos - r->token_pos) > MEMCACHE_MAX_KEY_LENGTH) {
                    wheatLog(WHEAT_WARNING,
                             "parsed bad type %d with key prefix '%d' and length %d that exceeds "
                             "maximum key length", r->type, r->token_pos, pos);
                    goto error;
                }

                arrayPush(r->keys, r->key);
                r->key = wstrNewLen(NULL, 64);

                r->token_pos = -1;

                /* get next state */
                if (memcache_storage(r)) {
                    state = SW_SPACES_BEFORE_FLAGS;
                } else if (memcache_arithmetic(r)) {
                    state = SW_SPACES_BEFORE_NUM;
                } else if (memcache_delete(r)) {
                    state = SW_RUNTO_CRLF;
                } else if (memcache_retrieval(r)) {
                    state = SW_SPACES_BEFORE_KEYS;
                } else {
                    state = SW_RUNTO_CRLF;
                }

                if (ch == CR) {
                    if (memcache_storage(r) || memcache_arithmetic(r)) {
                        goto error;
                    }
                    pos--; /* go back by 1 byte */
                }
            }

            break;

        case SW_SPACES_BEFORE_KEYS:
            ASSERT(memcache_retrieval(r));
            switch (ch) {
            case ' ':
                break;

            case CR:
                state = SW_ALMOST_DONE;
                break;

            default:
                r->token_pos = -1;
                pos--; /* go back by 1 byte */
                state = SW_KEY;
            }

            break;

        case SW_SPACES_BEFORE_FLAGS:
            if (ch != ' ') {
                if (!isdigit(ch)) {
                    goto error;
                }
                r->flag = ch - '0';
                state = SW_FLAGS;
            }

            break;

        case SW_FLAGS:
            if (isdigit(ch)) {
                r->flag = r->flag * 10 + (ch - '0');
            } else if (ch == ' ') {
                state = SW_SPACES_BEFORE_EXPIRY;
            } else {
                goto error;
            }

            break;

        case SW_SPACES_BEFORE_EXPIRY:
            if (ch != ' ') {
                if (!isdigit(ch)) {
                    goto error;
                }
                /* expiry_start <- p; expiry <- ch - '0' */
                r->expire = ch - '0';
                state = SW_EXPIRY;
            }

            break;

        case SW_EXPIRY:
            if (isdigit(ch)) {
                r->expire = r->expire * 10 + (ch - '0');
            } else if (ch == ' ') {
                state = SW_SPACES_BEFORE_VLEN;
            } else {
                goto error;
            }

            break;

        case SW_SPACES_BEFORE_VLEN:
            if (ch != ' ') {
                if (!isdigit(ch)) {
                    goto error;
                }
                /* vlen_start <- p */
                r->vlen = ch - '0';
                state = SW_VLEN;
            }

            break;

        case SW_VLEN:
            if (isdigit(ch)) {
                r->vlen = r->vlen * 10 + (uint32_t)(ch - '0');
            } else if (memcache_cas(r)) {
                if (ch != ' ') {
                    goto error;
                }
                pos--; /* go back by 1 byte */
                state = SW_SPACES_BEFORE_CAS;
            } else if (ch == ' ' || ch == CR) {
                pos--; /* go back by 1 byte */
                state = SW_RUNTO_CRLF;
            } else {
                goto error;
            }

            break;

        case SW_SPACES_BEFORE_CAS:
            if (ch != ' ') {
                if (!isdigit(ch)) {
                    goto error;
                }
                /* cas_start <- p; cas <- ch - '0' */
                r->cas = ch - '0';
                state = SW_CAS;
            }

            break;

        case SW_CAS:
            if (isdigit(ch)) {
                r->cas = r->cas * 10 + (ch - '0');
            } else if (ch == ' ' || ch == CR) {
                pos--; /* go back by 1 byte */
                state = SW_RUNTO_CRLF;
            } else {
                goto error;
            }

            break;


        case SW_RUNTO_VAL:
            switch (ch) {
            case LF:
                /* val_start <- p + 1 */
                state = SW_VAL;
                r->vlen_left = r->vlen;
                break;

            default:
                goto error;
            }

            break;

        case SW_VAL:
            if (r->vlen_left) {
                left = (s->len - pos) >= r->vlen ? (r->vlen) : (s->len-pos);
                val_s.len = left;
                val_s.data = &s->data[pos];
                arrayPush(r->vals, &val_s);
                r->vlen_left -= left;
                pos += left;
            }
            switch (s->data[pos]) {
            case CR:
                state = SW_ALMOST_DONE;
                break;

            default:
                goto error;
            }

            break;

        case SW_SPACES_BEFORE_NUM:
            if (ch != ' ') {
                if (!isdigit(ch)) {
                    goto error;
                }
                r->num = ch - '0';
                state = SW_NUM;
            }

            break;

        case SW_NUM:
            if (isdigit(ch)) {
                r->num = r->num * 10 + ch - '0';
            } else if (ch == ' ' || ch == CR) {
                pos--; /* go back by 1 byte */
                state = SW_RUNTO_CRLF;
            } else {
                goto error;
            }

            break;

        case SW_RUNTO_CRLF:
            switch (ch) {
            case ' ':
                break;

            case 'n':
                if (memcache_storage(r) || memcache_arithmetic(r) || memcache_delete(r)) {
                    state = SW_NOREPLY;
                    r->token_pos = pos;
                } else {
                    goto error;
                }

                break;

            case CR:
                if (memcache_storage(r)) {
                    state = SW_RUNTO_VAL;
                } else {
                    state = SW_ALMOST_DONE;
                }

                break;

            default:
                goto error;
            }

            break;

        case SW_NOREPLY:
            switch (ch) {
            case ' ':
            case CR:
                r->noreply_banner = wstrCatLen(r->noreply_banner, (char*)&s->data[r->token_pos], pos - r->token_pos);
                if (str7icmp(r->noreply_banner, 'n', 'o', 'r', 'e', 'p', 'l', 'y')) {
                    ASSERT(memcache_storage(r) || memcache_arithmetic(r) || memcache_delete(r));
                    /* noreply_end <- p - 1 */
                    r->noreply = 1;
                    state = SW_AFTER_NOREPLY;
                    pos--; /* go back by 1 byte */
                } else {
                    goto error;
                }
            }

            break;

        case SW_AFTER_NOREPLY:
            switch (ch) {
            case ' ':
                break;

            case CR:
                if (memcache_storage(r)) {
                    state = SW_RUNTO_VAL;
                } else {
                    state = SW_ALMOST_DONE;
                }
                break;

            default:
                goto error;
            }

            break;

        case SW_CRLF:
            switch (ch) {
            case ' ':
                break;

            case CR:
                state = SW_ALMOST_DONE;
                break;

            default:
                goto error;
            }

            break;

        case SW_ALMOST_DONE:
            switch (ch) {
            case LF:
                /* req_end <- p */
                goto done;

            default:
                goto error;
            }

            break;

        case SW_SENTINEL:
        default:
            ASSERT(0);
            break;

        }
        pos++;
    }

    if (state == SW_REQ_TYPE) {
        r->command = wstrCatLen(r->command, (char*)&s->data[r->token_pos], pos - r->token_pos);
    } else if (state == SW_KEY) {
        r->key = wstrCatLen(r->key, (char*)&s->data[r->token_pos], pos - r->token_pos);
    } else if (state == SW_NOREPLY) {
        r->noreply_banner = wstrCatLen(r->noreply_banner, (char*)&s->data[r->token_pos], pos - r->token_pos);
    }

done:
    r->stage = state;
    wheatLog(WHEAT_DEBUG, "parsed successfully type %d state %d", r->type, r->stage);
    return pos;

error:
    wheatLog(WHEAT_DEBUG, "parsed failed type %d state %d: %s", r->type, r->stage,
             s->data);
    return -1;
}


int parseMemcache(struct conn *c, struct slice *slice, size_t *out)
{
    ssize_t nparsed;
    struct memcacheProcData *memcache_data = c->protocol_data;

    nparsed = memcacheParseReq(memcache_data, slice);

    if (nparsed == -1) {
        return WHEAT_WRONG;
    }
    if (out) *out = nparsed;
    slice->len = nparsed;
    if (MEMCACHE_FINISHED(memcache_data)) {
        return WHEAT_OK;
    }
    return 1;
}

void *initMemcacheData()
{
    struct memcacheProcData *data = wmalloc(sizeof(struct memcacheProcData));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    data->command = wstrNewLen(NULL, 8);
    data->key = wstrNewLen(NULL, 64);
    data->keys = arrayCreate(sizeof(wstr), 1);
    data->vals = arrayCreate(sizeof(struct slice), 2);
    data->stage = SW_START;
    data->type = UNKNOWN;
    return data;
}

void freeMemcacheData(void *d)
{
    struct memcacheProcData *data = d;
    if (data->command)
        wstrFree(data->command);
    if (data->key)
        wstrFree(data->key);
    if (data->keys) {
        arrayEach(data->keys, (void(*)(void *))wstrFree);
        arrayDealloc(data->keys);
    }
    if (data->vals)
        arrayDealloc(data->vals);
    wfree(d);
}

int initMemcache()
{
    return WHEAT_OK;
}

void deallocMemcache()
{
}

int spotMemcache(struct conn *c)
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

enum memcacheCommand getMemcacheCommand(void *data)
{
    struct memcacheProcData *d = data;
    return d->type;
}

struct array *getMemcacheKeys(void *data)
{
    struct memcacheProcData *d = data;
    return d->keys;
}

wstr getMemcacheKey(void *data)
{
    struct memcacheProcData *d = data;
    return d->key;

}

uint64_t getMemcacheCas(void *data)
{
    struct memcacheProcData *d = data;
    return d->cas;
}

uint64_t getMemcacheValLen(void *data)
{
    struct memcacheProcData *d = data;
    return d->vlen;
}

uint64_t getMemcacheNum(void *data)
{
    struct memcacheProcData *d = data;
    return d->num;
}

uint64_t getMemcacheFlag(void *data)
{
    struct memcacheProcData *d = data;
    return d->flag;
}

struct array *getMemcacheVal(void *data)
{
    struct memcacheProcData *d = data;
    return d->vals;
}

int sendMemcacheResponse(struct conn *c, const char *data, uint64_t len)
{
    struct slice s = {(uint8_t *)data, len};
    return sendClientData(c, &s);
}
