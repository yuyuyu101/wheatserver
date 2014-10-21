// Memcache protocol parse implemetation
//
// Copyright (c) 2014 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#ifndef WHEATSERVER_PROTOCOL_MEMCACHE_PROTO_MEMCACHE_H
#define WHEATSERVER_PROTOCOL_MEMCACHE_PROTO_MEMCACHE_H

#include "../protocol.h"

enum memcacheCommand {
    REQ_MC_GET,
    REQ_MC_GETS,
    REQ_MC_DELETE,
    REQ_MC_CAS,
    REQ_MC_SET,
    REQ_MC_ADD,
    REQ_MC_REPLACE,
    REQ_MC_APPEND,
    REQ_MC_PREPEND,
    REQ_MC_INCR,
    REQ_MC_DECR,
    REQ_MC_QUIT,
    UNKNOWN
};

enum memcacheCommand getMemcacheCommand(void *data);
struct array *getMemcacheKeys(void *data);
wstr getMemcacheKey(void *data);
uint64_t getMemcacheCas(void *data);
uint64_t getMemcacheNum(void *data);
uint64_t getMemcacheValLen(void *data);
uint64_t getMemcacheFlag(void *data);
struct array *getMemcacheVal(void *data);
int sendMemcacheResponse(struct conn *c, struct slice *s);

#endif
