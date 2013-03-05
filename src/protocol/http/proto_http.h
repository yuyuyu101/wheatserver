#ifndef WHEATSERVER_PROTO_HTTP_H
#define WHEATSERVER_PROTO_HTTP_H

#include "../protocol.h"
#include "http_parser.h"

#define TRANSFER_ENCODING "Transfer-Encoding"
#define CONTENT_LENGTH    "Content-Length"
#define CONTENT_TYPE      "Content-Type"
#define CONNECTION        "Connection"

struct httpData {
    http_parser *parser;
    struct dictEntry *last_entry;
    int last_was_value;
    int complete;
    int headers_sent;
    int send;
    int is_chunked;

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
int httpSendBody(struct client *client, const char *data, size_t len);
void fillResInfo(struct httpData *, int status, const char *);
int httpSendHeaders(struct client *client);
void sendResponse500(struct client *c);
void sendResponse404(struct client *c);
void appendToResHeaders(struct client *c, const char *field,
        const char *value);

void logAccess(struct client *client);

#endif
