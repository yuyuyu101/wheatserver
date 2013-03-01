#include "wheatserver.h"
#include "proto_http.h"

static FILE *access_log_fp = NULL;
static struct http_parser_settings settings;

const char *URL_SCHEME[] = {
    "http",
    "https"
};

const char *PROTOCOL_VERSION[] = {
    "HTTP/1.0",
    "HTTP/1.1"
};

char *httpDate()
{
    static char buf[255];
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    return buf;
}

int is_chunked(int response_length, const char *version, int status)
{
    if (response_length == 0)
        return 0;
    else if (version == PROTOCOL_VERSION[0])
        return 0;
    else if (status == 304 || status == 204)
        return 0;
    return 1;
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

struct list *createResHeader(struct client *client)
{
    struct httpData *http_data = client->protocol_data;
    const char *connection = connectionField(client);
    char buf[256];
    struct list *headers = createList();
    listSetFree(headers, (void (*)(void *))wstrFree);

    snprintf(buf, 255, "%s %d %s\r\n", http_data->protocol_version,
            http_data->res_status, http_data->res_status_msg);
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    snprintf(buf, 255, "Server: %s\r\n", Server.master_name);
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    snprintf(buf, 255, "Date: %s\r\n", httpDate());
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    if (connection) {
        snprintf(buf, 255, "Connection: %s\r\n", connection);
        if (appendToListTail(headers, wstrNew(buf)) == NULL)
            goto cleanup;
    }

    if (is_chunked(http_data->response_length, http_data->protocol_version, http_data->res_status)) {
        snprintf(buf, 255, "Transfer-Encoding: chunked\r\n");
        if (appendToListTail(headers, wstrNew(buf)) == NULL)
            goto cleanup;
    }
    return headers;

cleanup:
    freeList(headers);
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
    wstr host = *h, port = *p;
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
    data->body = wstrCatLen(data->body, at, (int)len);
    if (data->body == NULL)
        return 1;
    else
        return 0;
}

int on_header_complete(http_parser *parser)
{
    struct httpData *data = parser->data;
    data->complete = 1;
    if (http_should_keep_alive(parser) == 0)
        data->keep_live = 0;
    else
        data->keep_live = 1;
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
    size_t recved = wstrlen(client->buf);
    struct httpData *http_data = client->protocol_data;

    nparsed = http_parser_execute(http_data->parser, &settings, client->buf, recved);

    if (http_data->parser->upgrade) {
        /* handle new protocol */
        wheatLog(WHEAT_WARNING, "parseHttp() handle new protocol: %s", client->buf);
        if (http_data->parser->http_minor == 0)
            http_data->upgrade = 1;
        else
            return WHEAT_WRONG;
    }
    wstrRange(client->buf, nparsed, 0);
    if (http_data->complete) {
        http_data->method = http_method_str(http_data->parser->method);
        if (http_data->parser->http_minor == 0)
            http_data->protocol_version = PROTOCOL_VERSION[0];
        else
            http_data->protocol_version = PROTOCOL_VERSION[1];
        return WHEAT_OK;
    }
    if (nparsed != recved) {
        /* Handle error. Usually just close the connection. */
        wheatLog(WHEAT_WARNING, "parseHttp() nparsed %d != recved %d: %s", nparsed, recved, client->buf);
        return WHEAT_WRONG;
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
    data->parser = malloc(sizeof(struct http_parser));
    data->parser->data = data;
    http_parser_init(data->parser, HTTP_REQUEST);
    data->req_headers = dictCreate(&wstrDictType);
    data->url_scheme = URL_SCHEME[0];
    data->query_string = NULL;
    data->keep_live = 1;
    data->upgrade = 0;
    data->path = NULL;
    data->last_was_value = 0;
    data->last_entry = NULL;
    data->complete = 0;
    data->method = NULL;
    data->protocol_version = NULL;
    data->body = wstrEmpty();
    data->res_status = 0;
    data->res_status_msg = wstrEmpty();
    data->response_length = 0;
    data->res_headers = dictCreate(&wstrDictType);
    if (data && data->parser)
        return data;
    return NULL;
}

void freeHttpData(void *data)
{
    struct httpData *d = data;
    if (d->req_headers) {
        dictRelease(d->req_headers);
    }
    wstrFree(d->body);
    wstrFree(d->query_string);
    free(d->parser);
    wstrFree(d->res_status_msg);
    if (d->res_headers) {
        dictRelease(d->res_headers);
    }
    free(d);
}

static void openAccessLog(char *access_log)
{
    if (access_log == NULL)
        access_log_fp = NULL;
    else if (!strcasecmp(access_log, "stdout"))
        access_log_fp = stdout;
    else
        access_log_fp = fopen(access_log, "a");
}

void initHttp()
{
    struct configuration *conf;
    char *access_log;
    conf = getConfiguration("access-log");
    access_log = conf->target.ptr;
    if (access_log && access_log_fp == NULL) {
        openAccessLog(access_log);
        if (!access_log_fp) {
            wheatLog(WHEAT_WARNING, "init Http failed");
            halt(1);
        }
    }
    memset(&settings, 0 , sizeof(http_parser_settings));
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_header_complete;
    settings.on_body = on_body;
    settings.on_url = on_url;
}

void deallocHttp()
{
    if (access_log_fp)
        fclose(access_log_fp);
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
    if (!access_log_fp)
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

    ret = fprintf(access_log_fp, "%s %s %s [%s] %s %s \"%s\" %s %s\n",
            remote_addr, hyphen, user, datetime, request, status_str,
            resp_length, refer, user_agent);
    wheatLog(WHEAT_DEBUG, "fprintf %d", ret);
    if (ret == -1)
        wheatLog(WHEAT_WARNING, "log access failed: %s", strerror(errno));
    else
        fflush(access_log_fp);
}
