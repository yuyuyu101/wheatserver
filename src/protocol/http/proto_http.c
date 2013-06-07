// Http protocol module implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "proto_http.h"

static FILE *AccessFp = NULL;
static struct http_parser_settings HttpPaserSettings;
#define WHEAT_BODY_LEN 10

int httpSpot(struct conn*);
int parseHttp(struct conn *, struct slice *, size_t *);
void *initHttpData();
void freeHttpData(void *data);
int initHttp();
void deallocHttp();

// Http
static struct configuration HttpConf[] = {
    {"access-log",        2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"document-root",     2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
};

struct protocol ProtocolHttp = {
    httpSpot, parseHttp, initHttpData, freeHttpData,
        initHttp, deallocHttp
};

struct moduleAttr ProtocolHttpAttr = {
    "Http", PROTOCOL, {.protocol=&ProtocolHttp}, NULL, 0,
    HttpConf, sizeof(HttpConf)/sizeof(struct configuration),
    NULL, 0
};

struct staticHandler {
    wstr abs_path;
    wstr root;
    wstr static_dir;
};

struct httpBody {
    struct slice *body;
    struct slice *curr_body;
    struct slice *end_body;
    int body_len;
    int slice_len;
};

struct httpData {
    //Intern use
    http_parser *parser;
    struct dictEntry *last_entry;
    unsigned last_was_value:1;
    unsigned complete:1;
    unsigned is_chunked_in_header:1;
    unsigned upgrade:1;
    unsigned keep_live:1;
    unsigned headers_sent:1;
    unsigned can_compress:1;
    unsigned send;

    wstr query_string;
    wstr path;
    struct httpBody body;
    const char *url_scheme;
    const char *method;
    const char *protocol_version;
    struct dict *req_headers;

    int res_status;
    wstr res_status_msg;
    int response_length;
    struct dict *res_headers;
    wstr send_header;
};

static struct staticHandler StaticPathHandler;

const char *URL_SCHEME[] = {
    "http",
    "https"
};

const char *PROTOCOL_VERSION[] = {
    "HTTP/1.0",
    "HTTP/1.1"
};

int convertHttpDate(time_t date, char *buf, size_t len)
{
    struct tm tm = *localtime(&date);
    long ret = strftime(buf, len, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    if (ret < 0 || ret > len)
        return -1;
    return 0;
}

time_t fromHttpDate(char *buf)
{
    struct tm tm;
    char *ret = strptime(buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    if (ret == NULL)
        return -1;
    return mktime(&tm);
}

char *httpDate()
{
    static char buf[255];
    static time_t now = 0;
    if (now != Server.cron_time.tv_sec || now == 0) {
        now = Server.cron_time.tv_sec;
        convertHttpDate(now, buf, sizeof(buf));
    }
    return buf;
}

const char *httpGetUrlScheme(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->url_scheme;
}

const char *httpGetMethod(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->method;
}

const char *httpGetProtocolVersion(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->protocol_version;
}

const wstr httpGetQueryString(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->query_string;
}

const wstr httpGetPath(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->path;
}

const struct slice *httpGetBodyNext(struct conn *c)
{
    struct httpData *data;
    struct slice *s;

    data = c->protocol_data;
    s = data->body.curr_body;
    if (s == data->body.end_body)
        return NULL;
    data->body.curr_body++;
    return s;
}

int httpBodyGetSize(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->body.body_len;
}

struct dict *httpGetReqHeaders(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->req_headers;
}

int ishttpHeaderSended(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->headers_sent;
}

int httpGetResStatus(struct conn *c)
{
    return ((struct httpData*)c->protocol_data)->res_status;
}

static int isChunked(struct httpData *http_data)
{
    return http_data->is_chunked_in_header &&
        http_data->response_length == 0 &&
        http_data->protocol_version == PROTOCOL_VERSION[1] &&
        http_data->res_status == 200;
}

static int enlargeHttpBody(struct httpBody *body)
{
    size_t curr = body->curr_body - body->body, end = body->end_body - body->body;
    if (body->slice_len == 0){
        body->slice_len = WHEAT_BODY_LEN;
    } else
        body->slice_len *= 2;
    body->body = wrealloc(body->body, sizeof(struct slice)*body->slice_len);
    if (body->body == NULL)
        return -1;
    body->curr_body = body->body + curr;
    body->end_body = body->body + end;
    return 0;
}

static const char *connectionField(struct conn *c)
{
    char *connection = NULL;
    struct httpData *data = c->protocol_data;

    if (data->upgrade) {
        connection = "upgrade";
    } else if (data->keep_live) {
        connection = "keep-live";
    } else if (!data->keep_live) {
        connection = "close";
        setClientClose(c);
    }
    return connection;
}

int appendToResHeaders(struct conn *c, const char *field,
        const char *value)
{
    struct httpData *http_data;
    int ret;

    http_data = c->protocol_data;
    ret = dictAdd(http_data->res_headers, wstrNew(field), wstrNew(value));
    if (ret == DICT_WRONG)
        return -1;
    return 0;
}

void fillResInfo(struct conn *c, int status, const char *msg)
{
    struct httpData *data = c->protocol_data;
    data->res_status = status;
    data->res_status_msg = wstrNew(msg);
}

static wstr defaultResHeader(struct conn *c, wstr headers)
{
    char buf[256];
    int ret;
    struct httpData *http_data = c->protocol_data;
    ASSERT(http_data->res_status && http_data->res_status_msg);

    ret = snprintf(buf, sizeof(buf), "%s %d %s\r\n", http_data->protocol_version,
            http_data->res_status, http_data->res_status_msg);
    if (ret < 0 || ret > sizeof(buf))
        goto cleanup;
    if ((headers = wstrCatLen(headers, buf, ret)) == NULL)
        goto cleanup;

    ret = snprintf(buf, 255, "Server: %s\r\n", Server.master_name);
    if (ret < 0 || ret > sizeof(buf))
        goto cleanup;
    if ((headers = wstrCatLen(headers, buf, ret)) == NULL)
        goto cleanup;
    ret = snprintf(buf, 255, "Date: %s\r\n", httpDate());
    if (ret < 0 || ret > sizeof(buf))
        goto cleanup;
    if ((headers = wstrCatLen(headers, buf, ret)) == NULL)
        goto cleanup;
    return headers;;

cleanup:
    wstrFree(headers);
    return NULL;
}

/* `value` is the value of http header x-forwarded-for.
 * `h` and `p` as value-argument returned.
 * avoid memory leak, you should free `h` and `p` after used.
 * `h` and `p` may be NULL if value is incorrect.
 * refer: http://tools.ietf.org/html/draft-petersson-forwarded-for-01#section-6.1
 */
void parserForward(wstr value, wstr *h, wstr *p)
{
    wstr remote_addr = NULL;
    wstr host, port;
    int count;
    /* took the last one http://en.wikipedia.org/wiki/X-Forwarded-For */
    if (strchr(value, ',') == NULL)
        remote_addr = wstrDup(value);
    else {
        wstr *ips = wstrNewSplit(value, ",", 1, &count);
        if (count >= 1) {
            remote_addr = wstrStrip(wstrDup(ips[count-1]), " ");
        }
        wstrFreeSplit(ips, count);
    }
    /* judge ip format v4 or v6 */
    char *left_side, *right_side;
    if ((left_side = strchr(remote_addr, '[')) != NULL &&
            (right_side = strchr(remote_addr, ']')) != NULL && left_side < right_side) {
        host = wstrStrip(wstrNewLen(left_side+1, (int)(right_side-left_side-1)), " ");
    } else if ((left_side = strchr(remote_addr, ':')) != NULL && strchr(left_side+1, ':') == NULL && left_side > remote_addr) {
        host = wstrStrip(wstrNewLen(remote_addr, (int)(left_side-remote_addr)), " ");
        wstrLower(host);
    } else
        host = wstrStrip(wstrDup(remote_addr), " ");

    left_side = strrchr(remote_addr, ']');
    if (left_side)
        remote_addr = wstrRange(remote_addr, (int)(left_side-remote_addr+1), -1);
    right_side = strchr(remote_addr, ':');
    if (right_side && strchr(right_side+1, ':') == NULL)
        port = wstrNew(right_side+1);
    else
        port = wstrNew("80");

    *h = host;
    *p = port;
    wstrFree(remote_addr);
}

/* three situation called
 * 1. init header, no header exists
 * 2. new header comes
 * 3. last header remainder comes
 */
int on_header_field(http_parser *parser, const char *at, size_t len)
{
    int ret;
    wstr key;
    struct httpData *data = parser->data;

    if (!data->last_was_value && data->last_entry != NULL) {
        key = data->last_entry->key;
        ret = dictDeleteNoFree(data->req_headers, key);
        if (ret == DICT_WRONG)
            return 1;
        key = wstrCatLen(key, at, len);
    }
    else
        key = wstrNewLen(at, (int)len);
    data->last_was_value = 0;

    data->last_entry = dictReplaceRaw(data->req_headers, key);
    if (data->last_entry == NULL)
        return 1;
    return 0;
}

/* two situation called
 * 1. new header value comes
 * 2. last header value remainder comes
 */
int on_header_value(http_parser *parser, const char *at, size_t len)
{
    wstr value;
    struct httpData *data = parser->data;
    if (data->last_was_value) {
        value = dictGetVal(data->last_entry);
        value = wstrCatLen(value, at, len);
    } else
        value = wstrNewLen(at, (int)len);

    data->last_was_value = 1;
    if (value == NULL)
        return 1;
    dictSetVal(data->req_headers, data->last_entry, value);
    return 0;
}

int on_body(http_parser *parser, const char *at, size_t len)
{
    struct httpData *data;
    struct httpBody *body;

    data = parser->data;
    body = &data->body;
    if (body->end_body == body->body + body->slice_len) {
        if (enlargeHttpBody(&data->body) == -1)
            return 1;
        body = &data->body;
    }
    sliceTo(body->end_body, (uint8_t *)at, len);
    body->end_body++;
    body->body_len += len;
    return 0;
}

int on_header_complete(http_parser *parser)
{
    struct httpData *data = parser->data;
    if (http_should_keep_alive(parser) == 0)
        data->keep_live = 0;
    else
        data->keep_live = 1;
    return 0;
}

int on_message_complete(http_parser *parser)
{
    struct httpData *data = parser->data;
    data->complete = 1;
    return 0;
}

int on_url(http_parser *parser, const char *at, size_t len)
{
    struct http_parser_url parser_url;
    struct httpData *data = parser->data;

    memset(&parser_url, 0, sizeof(struct http_parser_url));
    if (http_parser_parse_url(at, len, 0, &parser_url))
        return 1;
    data->query_string = wstrNewLen(at+parser_url.field_data[UF_QUERY].off, parser_url.field_data[UF_QUERY].len);
    data->path = wstrNewLen(at+parser_url.field_data[UF_PATH].off, parser_url.field_data[UF_PATH].len);
    return 0;
}

int parseHttp(struct conn *c, struct slice *slice, size_t *out)
{
    size_t nparsed;
    struct httpData *http_data = c->protocol_data;

    nparsed = http_parser_execute(http_data->parser, &HttpPaserSettings, (const char *)slice->data, slice->len);

    if (nparsed != slice->len) {
        /* Handle error. Usually just close the connection. */
        wheatLog(WHEAT_WARNING, "parseHttp() nparsed %d != recved %d", nparsed, slice->len);
        return WHEAT_WRONG;
    }

    slice->len = nparsed;

    if (http_data->parser->upgrade) {
        /* handle new protocol */
        wheatLog(WHEAT_WARNING, "parseHttp() handle new protocol");
        if (http_data->parser->http_minor == 0)
            http_data->upgrade = 1;
        else
            return WHEAT_WRONG;
    }

    if (out) *out = nparsed;
    if (http_data->complete) {
        http_data->method = http_method_str(http_data->parser->method);
        if (http_data->parser->http_minor == 0)
            http_data->protocol_version = PROTOCOL_VERSION[0];
        else
            http_data->protocol_version = PROTOCOL_VERSION[1];
        return WHEAT_OK;
    }
    if (http_data->parser->http_errno) {
        wheatLog(
                WHEAT_WARNING,
                "http_parser error name: %s description: %s",
                http_errno_name(http_data->parser->http_errno),
                http_errno_description(http_data->parser->http_errno)
                );
        return WHEAT_WRONG;
    }
    return 1;
}

void *initHttpData()
{
    struct httpData *data = wmalloc(sizeof(struct httpData));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    data->parser = wmalloc(sizeof(struct http_parser));
    if (!data->parser) {
        wfree(data);
        return NULL;
    }
    data->parser->data = data;
    http_parser_init(data->parser, HTTP_REQUEST);
    data->url_scheme = URL_SCHEME[0];
    memset(&data->body, 0, sizeof(data->body));
    int ret = enlargeHttpBody(&data->body);
    if (ret == -1) {
        wfree(data);
        wfree(data->parser);
        return NULL;
    }
    data->req_headers = dictCreate(&wstrDictType);
    data->res_headers = dictCreate(&wstrDictType);
    data->send_header = wstrNewLen(NULL, 500);
    return data;
}

void freeHttpData(void *data)
{
    struct httpData *d = data;
    dictRelease(d->req_headers);
    dictRelease(d->res_headers);
    wstrFree(d->query_string);
    wfree(d->parser);
    wstrFree(d->res_status_msg);
    wstrFree(d->path);
    wstrFree(d->send_header);
    wfree(d->body.body);
    wfree(d);
}

static FILE *openAccessLog()
{
    struct configuration *conf;
    FILE *fp;
    char *access_log;

    conf = getConfiguration("access-log");
    access_log = conf->target.ptr;
    if (access_log == NULL)
        fp = NULL;
    else if (!strcasecmp(access_log, "stdout"))
        fp = stdout;
    else {
        fp = fopen(access_log, "a");
        if (!fp) {
            wheatLog(WHEAT_NOTICE, "open access log failed");
        }
    }

    return fp;
}

int initHttp()
{
    struct configuration *conf1, *conf2;

    AccessFp = openAccessLog();
    memset(&StaticPathHandler, 0 , sizeof(struct staticHandler));
    conf1 = getConfiguration("document-root");
    conf2 = getConfiguration("static-file-dir");
    if (conf1->target.ptr && conf2->target.ptr) {
        char path[1024];
        char *real_path = realpath(conf1->target.ptr, path);
        if (!real_path) {
            wheatLog(WHEAT_WARNING, "document-root %s is unvalid",
                    conf1->target.ptr);
            return WHEAT_WRONG;
        }
        StaticPathHandler.abs_path = wstrNew(real_path);
        StaticPathHandler.root = wstrDup(StaticPathHandler.abs_path);
        StaticPathHandler.static_dir = wstrNew(conf2->target.ptr);
        StaticPathHandler.abs_path = wstrCat(StaticPathHandler.abs_path,
                StaticPathHandler.static_dir);
        real_path = realpath(StaticPathHandler.abs_path, path);
        if (!real_path) {
            wheatLog(WHEAT_WARNING, "static-file-dir is unvalid");
            return WHEAT_WRONG;
        }
    }

    memset(&HttpPaserSettings, 0 , sizeof(HttpPaserSettings));
    HttpPaserSettings.on_header_field = on_header_field;
    HttpPaserSettings.on_header_value = on_header_value;
    HttpPaserSettings.on_headers_complete = on_header_complete;
    HttpPaserSettings.on_body = on_body;
    HttpPaserSettings.on_url = on_url;
    HttpPaserSettings.on_message_complete = on_message_complete;
    return WHEAT_OK;
}

void deallocHttp()
{
    if (AccessFp)
        fclose(AccessFp);
    wstrFree(StaticPathHandler.abs_path);
    memset(&StaticPathHandler, 0, sizeof(struct staticHandler));
}

static const char *apacheDateFormat()
{
    static char buf[255];
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    strftime(buf, sizeof buf, "%d/%m/%Y:%H:%M:%S %z", &tm);
    return buf;
}

void logAccess(struct conn *c)
{
    if (!AccessFp)
        return ;
    const char *remote_addr, *hyphen, *user, *datetime, *request;
    const char *refer, *user_agent;
    char status_str[100], resp_length[32];
    char buf[255];
    wstr temp;
    int ret;
    struct httpData *http_data = c->protocol_data;

    temp = wstrNew("Remote_Addr");
    remote_addr = dictFetchValue(http_data->req_headers, temp);
    if (!remote_addr)
        remote_addr = getConnIP(c);
    wstrFree(temp);

    hyphen = user = "-";
    datetime = apacheDateFormat();
    snprintf(buf, 255, "%s %s %s", http_data->method, http_data->path, http_data->protocol_version);
    request = buf;
    gcvt(http_data->res_status, 0, status_str);
    gcvt(http_data->response_length, 0, resp_length);

    temp = wstrNew("Referer");
    refer = dictFetchValue(http_data->req_headers, temp);
    if (!refer)
        refer = "-";
    wstrFree(temp);

    temp = wstrNew("User-Agent");
    user_agent = dictFetchValue(http_data->req_headers, temp);
    if (!user_agent)
        user_agent = "-";
    wstrFree(temp);

    ret = fprintf(AccessFp, "%s %s %s [%s] %s %s \"%s\" %s %s\n",
            remote_addr, hyphen, user, datetime, request, status_str,
            resp_length, refer, user_agent);
    if (ret == -1) {
        wheatLog(WHEAT_WARNING, "log access failed: %s", strerror(errno));
        fclose(AccessFp);
        AccessFp = openAccessLog();
        if (!AccessFp) {
            wheatLog(WHEAT_WARNING, "open access log failed");
            halt(1);
        }
    }
    else
        fflush(AccessFp);
}

/* Send a chunk of data */
int httpSendBody(struct conn *c, const char *data, size_t len)
{
    size_t tosend, restsend;
    ssize_t ret;
    struct slice slice;
    struct httpData *http_data;

    tosend = len;
    http_data = c->protocol_data;
    if (!len || !strcasecmp(http_data->method, "HEAD"))
        return 0;
    if (http_data->response_length != 0) {
        if (http_data->send > http_data->response_length)
            return 0;
        restsend = http_data->response_length - http_data->send;
        tosend = restsend > tosend ? tosend: restsend;
    } else if (isChunked(http_data))
        return 0;

    http_data->send += tosend;
    sliceTo(&slice, (uint8_t *)data, tosend);
    ret = sendClientData(c, &slice);
    if (ret == WHEAT_WRONG)
        return -1;
    return 0;
}

int httpSendHeaders(struct conn *c)
{
    struct httpData *http_data = c->protocol_data;
    int ok, len, ret, is_connection;
    struct dictIterator *iter;
    struct dictEntry *entry;
    const char *connection;
    char buf[256];
    wstr field, value, headers;
    struct slice slice;

    if (http_data->headers_sent)
        return 0;

    is_connection = 0;
    ok = 0;
    iter = dictGetIterator(http_data->res_headers);
    headers = defaultResHeader(c, http_data->send_header);
    while((entry = dictNext(iter)) != NULL) {
        field = dictGetKey(entry);
        value = dictGetVal(entry);
        ASSERT(field && value);
        if (!strncasecmp(field, TRANSFER_ENCODING,
                    sizeof(TRANSFER_ENCODING))) {
            if (!strncasecmp(value, CHUNKED, sizeof(CHUNKED)))
                http_data->is_chunked_in_header = 1;
        } else if (!strncasecmp(field, CONTENT_LENGTH, sizeof(CONTENT_LENGTH))) {
            http_data->response_length = atoi(value);
        } else if (!strncasecmp(field, CONNECTION, sizeof(CONNECTION))) {
            is_connection = 1;
        }
        ret = snprintf(buf, sizeof(buf), "%s: %s\r\n", field, value);
        if (ret == -1 || ret > sizeof(buf))
            goto cleanup;
        headers = wstrCatLen(headers, buf, ret);
    }
    if (!http_data->response_length && http_data->res_status != 302)
        http_data->keep_live = 0;
    if (!is_connection) {
        connection = connectionField(c);
        ret = snprintf(buf, sizeof(buf), "Connection: %s\r\n", connection);
        if (ret == -1 || ret > sizeof(buf))
            goto cleanup;
        if ((headers = wstrCatLen(headers, buf, ret)) == NULL)
            goto cleanup;
    }

    headers = wstrCatLen(headers, "\r\n", 2);
    sliceTo(&slice, (uint8_t *)headers, wstrlen(headers));

    len = sendClientData(c, &slice);
    if (len < 0) {
        goto cleanup;
    }
    http_data->headers_sent = 1;
    ok = 1;

cleanup:
    dictReleaseIterator(iter);
    return ok? 0 : -1;
}


void sendResponse404(struct conn *c)
{
    static const char body[] =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>404 Not Found --- From Wheatserver</title>\n"
        "</head><body>\n"
        "<h1>Not Found</h1>\n"
        "<p>The requested URL was not found on this server</p>\n"
        "</body></html>\n";
    fillResInfo(c, 404, "NotFound");

    if (!httpSendHeaders(c))
        httpSendBody(c, body, strlen(body));
}

void sendResponse500(struct conn *c)
{
    static const char body[] =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>500 Internal Error --- From Wheatserver</title>\n"
        "</head><body>\n"
        "<h1>Internal Error</h1>\n"
        "<p>The server encountered an unexpected condition which\n"
        "prevented it from fulfilling the request.</p>\n"
        "</body></html>\n";
    fillResInfo(c, 500, "Internal Error");

    if (!httpSendHeaders(c))
        httpSendBody(c, body, strlen(body));
}

int httpSpot(struct conn *c)
{
    struct app *app;
    int ret;
    struct httpData *http_data = c->protocol_data;
    wstr path = NULL;
    if (StaticPathHandler.abs_path && fromSameParentDir(StaticPathHandler.static_dir, http_data->path)) {
        path = wstrNew(StaticPathHandler.root);
        path = wstrCat(path, http_data->path);
        app = spotApp("static-file");
    } else {
        app = spotApp("wsgi");
    }
    if (app->is_init) {
        ret = app->initApp(c->client->protocol);
        if (ret == WHEAT_WRONG) {
            wstrFree(path);
            return ret;
        }
        app->is_init = 1;
    }
    c->app = app;
    ret = initAppData(c);
    if (ret == WHEAT_WRONG) {
        wstrFree(path);
        wheatLog(WHEAT_WARNING, "init app data failed");
        return WHEAT_WRONG;
    }
    ret = app->appCall(c, path);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "app failed, exited");
        app->deallocApp();
        app->is_init = 0;
    }
    logAccess(c);
    wstrFree(path);
    finishConn(c);
    return ret;
}
