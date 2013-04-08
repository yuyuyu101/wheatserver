// Http protocol module implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_PROTO_HTTP_H
#define WHEATSERVER_PROTO_HTTP_H

#include "../protocol.h"
#include "http_parser.h"

#define TRANSFER_ENCODING    "Transfer-Encoding"
#define CONTENT_LENGTH       "Content-Length"
#define CONTENT_TYPE         "Content-Type"
#define CONNECTION           "Connection"
#define LAST_MODIFIED        "Last-Modified"
#define IF_MODIFIED_SINCE    "If-Modified-Since"
#define CHUNKED              "Chunked"
#define HTTP_CONTINUE        "HTTP/1.1 100 Continue\r\n\r\n"

// Http protocol API
const wstr httpGetPath(struct conn *c);
const wstr httpGetQueryString(struct conn *c);
const struct slice *httpGetBodyNext(struct conn *c);
int httpBodyGetSize(struct conn *c);
const char *httpGetUrlScheme(struct conn *c);
const char *httpGetMethod(struct conn *c);
const char *httpGetProtocolVersion(struct conn *c);
struct dict *httpGetReqHeaders(struct conn *c);
int ishttpHeaderSended(struct conn *c);
int httpGetResStatus(struct conn *c);
void parserForward(wstr value, wstr *h, wstr *p);
int convertHttpDate(time_t date, char *buf, size_t len);
time_t fromHttpDate(char *buf);
int httpSendBody(struct conn *c, const char *data, size_t len);
void fillResInfo(struct conn *c, int status, const char *msg);
int httpSendHeaders(struct conn *c);
void sendResponse500(struct conn *c);
void sendResponse404(struct conn *c);
int appendToResHeaders(struct conn *c, const char *field,
        const char *value);

void logAccess(struct conn *c);

#endif
