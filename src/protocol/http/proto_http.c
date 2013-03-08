#include "proto_http.h"

extern struct app appTable[];
static FILE *AccessFp = NULL;
static struct http_parser_settings HttpPaserSettings;
#define WHEAT_BODY_LEN 10

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
    if (now != Server.cron_time || now == 0) {
        now = Server.cron_time;
        convertHttpDate(now, buf, sizeof(buf));
    }
    return buf;
}

const char *httpGetUrlScheme(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->url_scheme;
}

const char *httpGetMethod(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->method;
}

const char *httpGetProtocolVersion(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->protocol_version;
}

const wstr httpGetQueryString(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->query_string;
}

const wstr httpGetPath(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->path;
}

const struct slice *httpGetBodyNext(struct client *c)
{
    struct httpData *data = c->protocol_data;
    struct slice *s = data->body.curr_body;
    if (s == data->body.end_body)
        return NULL;
    data->body.curr_body++;
    return s;
}

int httpBodyGetSize(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->body.body_len;
}

struct dict *httpGetReqHeaders(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->req_headers;
}

int ishttpHeaderSended(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->headers_sent;
}

int httpGetResStatus(struct client *c)
{
    struct httpData *data = c->protocol_data;
    return data->res_status;
}

int isChunked(struct httpData *http_data)
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
    body->body = realloc(body->body, sizeof(struct slice)*body->slice_len);
    if (body->body == NULL)
        return -1;
    body->curr_body = body->body + curr;
    body->end_body = body->body + end;
    return 0;
}

static const char *connectionField(struct client *c)
{
    char *connection = NULL;
    struct httpData *data = c->protocol_data;

    if (data->upgrade) {
        connection = "upgrade";
    } else if (data->keep_live) {
        connection = "keep-live";
    } else if (!data->keep_live) {
        connection = "close";
        c->should_close = 1;
    }
    return connection;
}

int appendToResHeaders(struct client *c, const char *field,
        const char *value)
{
    struct httpData *http_data = c->protocol_data;
    int ret = dictAdd(http_data->res_headers, wstrNew(field), wstrNew(value));
    if (ret == DICT_WRONG)
        return -1;
    return 0;
}

void fillResInfo(struct client *c, int status, const char *msg)
{
    struct httpData *data = c->protocol_data;
    data->res_status = status;
    data->res_status_msg = wstrNew(msg);
}

static wstr defaultResHeader(struct client *client)
{
    struct httpData *http_data = client->protocol_data;
    char buf[256];
    wstr headers = wstrEmpty();
    int ret;
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
    struct httpData *data = parser->data;
    wstr key;
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
    struct httpData *data = parser->data;
    wstr value;
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
    struct httpData *data = parser->data;
    struct httpBody *body = &data->body;
    if (body->end_body == body->body + body->slice_len) {
        int ret = enlargeHttpBody(&data->body);
        if (ret == -1)
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
    struct httpData *data = parser->data;
    struct http_parser_url parser_url;
    memset(&parser_url, 0, sizeof(struct http_parser_url));
    int ret = http_parser_parse_url(at, len, 0, &parser_url);
    if (ret)
        return 1;
    data->query_string = wstrNewLen(at+parser_url.field_data[UF_QUERY].off, parser_url.field_data[UF_QUERY].len);
    data->path = wstrNewLen(at+parser_url.field_data[UF_PATH].off, parser_url.field_data[UF_PATH].len);
    return 0;
}

int parseHttp(struct client *client)
{
    size_t nparsed;
    struct slice slice;
    msgRead(client->req_buf, &slice);
    struct httpData *http_data = client->protocol_data;

    nparsed = http_parser_execute(http_data->parser, &HttpPaserSettings, (const char *)slice.data, slice.len);

    if (nparsed != slice.len) {
        /* Handle error. Usually just close the connection. */
        wheatLog(WHEAT_WARNING, "parseHttp() nparsed %d != recved %d", nparsed, slice.len);
        return WHEAT_WRONG;
    }
    msgSetReaded(client->req_buf, nparsed);

    if (http_data->parser->upgrade) {
        /* handle new protocol */
        wheatLog(WHEAT_WARNING, "parseHttp() handle new protocol");
        if (http_data->parser->http_minor == 0)
            http_data->upgrade = 1;
        else
            return WHEAT_WRONG;
    }

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
    struct httpData *data = malloc(sizeof(struct httpData));
    if (!data)
        return NULL;
    data->parser = malloc(sizeof(struct http_parser));
    if (!data->parser) {
        free(data);
        return NULL;
    }
    memset(data, 0, sizeof(*data));
    data->parser->data = data;
    http_parser_init(data->parser, HTTP_REQUEST);
    data->url_scheme = URL_SCHEME[0];
    memset(&data->body, 0, sizeof(data->body));
    int ret = enlargeHttpBody(&data->body);
    if (ret == -1) {
        free(data);
        free(data->parser);
        return NULL;
    }
    data->req_headers = dictCreate(&wstrDictType);
    data->res_headers = dictCreate(&wstrDictType);
    return data;
}

void freeHttpData(void *data)
{
    struct httpData *d = data;
    dictRelease(d->req_headers);
    dictRelease(d->res_headers);
    wstrFree(d->query_string);
    free(d->parser);
    wstrFree(d->res_status_msg);
    wstrFree(d->path);
    free(d->body.body);
    free(d);
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

void initHttp()
{
    struct configuration *conf1, *conf2;

    AccessFp = openAccessLog();
    memset(&StaticPathHandler, 0 , sizeof(struct staticHandler));
    conf1 = getConfiguration("document-root");
    conf2 = getConfiguration("static-file-dir");
    if (conf1->target.ptr && conf2->target.ptr) {
        StaticPathHandler.abs_path = wstrNew(conf1->target.ptr);
        StaticPathHandler.root = wstrDup(StaticPathHandler.abs_path);
        StaticPathHandler.static_dir = wstrNew(conf2->target.ptr);
        StaticPathHandler.abs_path = wstrCat(StaticPathHandler.abs_path,
                StaticPathHandler.static_dir);
    }

    memset(&HttpPaserSettings, 0 , sizeof(HttpPaserSettings));
    HttpPaserSettings.on_header_field = on_header_field;
    HttpPaserSettings.on_header_value = on_header_value;
    HttpPaserSettings.on_headers_complete = on_header_complete;
    HttpPaserSettings.on_body = on_body;
    HttpPaserSettings.on_url = on_url;
    HttpPaserSettings.on_message_complete = on_message_complete;
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

void logAccess(struct client *client)
{
    if (!AccessFp)
        return ;
    struct httpData *http_data = client->protocol_data;
    const char *remote_addr, *hyphen, *user, *datetime, *request;
    const char *refer, *user_agent;
    char status_str[100], resp_length[32];
    char buf[255];
    wstr temp;
    int ret;

    temp = wstrNew("Remote_Addr");
    remote_addr = dictFetchValue(http_data->req_headers, temp);
    if (!remote_addr)
        remote_addr = client->ip;
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
int httpSendBody(struct client *client, const char *data, size_t len)
{
    struct httpData *http_data = client->protocol_data;
    size_t tosend = len, restsend;
    ssize_t ret;
    struct slice slice;
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
    ret = WorkerProcess->worker->sendData(client, &slice);
    if (ret == WHEAT_WRONG)
        return -1;
    return 0;
}

static void httpSendHeadersAction(struct client *c)
{
    struct httpData *http_data = c->protocol_data;
}

int httpSendHeaders(struct client *client)
{
    struct httpData *http_data = client->protocol_data;
    int len;
    int ok = 0;

    if (http_data->headers_sent)
        return 0;
    struct dictIterator *iter = dictGetIterator(http_data->res_headers);
    struct dictEntry *entry = NULL;
    char buf[256];
    int ret, is_connection = 0;
    wstr field, value;
    wstr headers = defaultResHeader(client);
    while((entry = dictNext(iter)) != NULL) {
        field = dictGetKey(entry);
        value = dictGetVal(entry);
        ASSERT(field && value);
        if (client->res_buf == NULL) {
            goto cleanup;
        }
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
    if (!is_connection) {
        const char *connection = connectionField(client);
        ret = snprintf(buf, sizeof(buf), "Connection: %s\r\n", connection);
        if (ret == -1 || ret > sizeof(buf))
            goto cleanup;
        if ((headers = wstrCatLen(headers, buf, ret)) == NULL)
            goto cleanup;
    }

    headers = wstrCatLen(headers, "\r\n", 2);
    struct slice slice;
    sliceTo(&slice, (uint8_t *)headers, wstrlen(headers));

    len = WorkerProcess->worker->sendData(client, &slice);
    if (len < 0) {
        goto cleanup;
    }
    http_data->headers_sent = 1;
    ok = 1;

cleanup:
    wstrFree(headers);
    dictReleaseIterator(iter);
    return ok? 0 : -1;
}


void sendResponse404(struct client *c)
{
    struct httpData *http_data = c->protocol_data;
    static const char body[] =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>404 Not Found --- From Wheatserver</title>\n"
        "</head><body>\n"
        "<h1>Not Found</h1>\n"
        "<p>The requested URL was not found on this server</p>\n"
        "</body></html>\n";
    fillResInfo(c, 404, "NotFound");
    http_data->response_length = sizeof(body);

    if (!httpSendHeaders(c))
        httpSendBody(c, body, strlen(body));
}

void sendResponse500(struct client *c)
{
    struct httpData *http_data = c->protocol_data;
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
    http_data->response_length = sizeof(body);

    if (!httpSendHeaders(c))
        httpSendBody(c, body, strlen(body));
}

int httpSpot(struct client *c)
{
    struct httpData *http_data = c->protocol_data;
    int i = 0, ret;
    wstr path = NULL;
    if (StaticPathHandler.abs_path && fromSameParentDir(StaticPathHandler.static_dir, http_data->path)) {
        path = wstrNew(StaticPathHandler.root);
        path = wstrCat(path, http_data->path);
        i = 1;
    }
    if (!appTable[i].is_init) {
        appTable[i].initApp();
        appTable[i].is_init = 1;
    }
    c->app = &appTable[i];
    c->app_private_data = c->app->initAppData(c);
    ret = appTable[i].appCall(c, path);
    c->app->freeAppData(c->app_private_data);
    logAccess(c);
    wstrFree(path);
    if (http_data->response_length == 0)
        c->should_close = 1;
    return ret;
}
