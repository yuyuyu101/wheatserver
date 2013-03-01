#ifndef WHEATSERVER_PROTO_HTTP_H
#define WHEATSERVER_PROTO_HTTP_H

#include "wstr.h"
#include "http_parser.h"
#include "dict.h"

struct httpData {
    http_parser *parser;
    struct dict *req_headers;
    wstr query_string;
    wstr path;
    wstr body;
    int upgrade;
    int keep_live;
    const char *url_scheme;
    const char *method;
    const char *protocol_version;
    struct dictEntry *last_entry;
    int last_was_value;
    int complete;

    // Response
    int res_status;
    wstr res_status_msg;
    int response_length;
    struct dict *res_headers;
};

void parserForward(wstr value, wstr *h, wstr *p);
char *httpDate();
int is_chunked(int response_length, const char *version, int status);
struct list *createResHeader(struct client *client);

void logAccess(struct client *client);


#endif
