#ifndef WHEATSERVER_PROTO_HTTP_H
#define WHEATSERVER_PROTO_HTTP_H

#include "../protocol.h"
#include "http_parser.h"

struct httpData {
    http_parser *parser;
    struct dictEntry *last_entry;
    int last_was_value;
    int complete;
    int headers_sent;
    int send;

    // Read only
    struct dict *req_headers;
    wstr query_string;
    wstr path;
    wstr body;
    int upgrade;
    int keep_live;
    const char *url_scheme;
    const char *method;
    const char *protocol_version;

    // App write
    int res_status;
    wstr res_status_msg;
    int response_length;
    struct list *res_headers;
};

void parserForward(wstr value, wstr *h, wstr *p);
char *httpDate();
int is_chunked(int response_length, const char *version, int status);
int httpSendBody(struct client *client, const char *data, size_t len);
int httpSendHeaders(struct client *client);
void sendResponse500(struct client *c);
void sendResponse404(struct client *c);
void fillResInfo(struct httpData *, int, int status, const char *);

void logAccess(struct client *client);

#endif
