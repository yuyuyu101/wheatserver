#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include "wstr.h"
#include "http_parser.h"
#include "dict.h"

struct httpData {
    http_parser *parser;
    struct dict *headers;
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
};

int parseHttp(struct client *);
void parserForward(wstr value, wstr *h, wstr *p);
char *httpDate();
int is_chunked();
void *initHttpData();
void freeHttpData(void *data);

#endif
