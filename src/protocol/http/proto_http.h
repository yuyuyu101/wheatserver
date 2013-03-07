#ifndef WHEATSERVER_PROTO_HTTP_H
#define WHEATSERVER_PROTO_HTTP_H

#include "../protocol.h"
#include "http_parser.h"

#define TRANSFER_ENCODING "Transfer-Encoding"
#define CONTENT_LENGTH    "Content-Length"
#define CONTENT_TYPE      "Content-Type"
#define CONNECTION        "Connection"
#define CHUNKED           "Chunked"
#define HTTP_CONTINUE     "HTTP/1.1 100 Continue\r\n\r\n"

const wstr httpGetPath(struct client *c);
const wstr httpGetQueryString(struct client *c);
const struct slice *httpGetBodyNext(struct client *c);
int httpBodyGetSize(struct client *c);
const char *httpGetUrlScheme(struct client *c);
const char *httpGetMethod(struct client *c);
const char *httpGetProtocolVersion(struct client *c);
struct dict *httpGetReqHeaders(struct client *c);
int ishttpHeaderSended(struct client *c);
int httpGetResStatus(struct client *c);
void parserForward(wstr value, wstr *h, wstr *p);
char *httpDate();
int httpSendBody(struct client *client, const char *data, size_t len);
void fillResInfo(struct client *c, int status, const char *msg);
int httpSendHeaders(struct client *client);
void sendResponse500(struct client *c);
void sendResponse404(struct client *c);
void appendToResHeaders(struct client *c, const char *field,
        const char *value);

void logAccess(struct client *client);

#endif
