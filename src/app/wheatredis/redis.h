// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H
#define WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H

#include "../application.h"

#define DIRTY    1
#define NONDIRTY 0

// struct token like virtual node in consistent hash
struct token {
    uint32_t value;
    // `server_idx` is simply the offset of redisServer.instances
    int server_idx;
    // `next_server` indicates the next real server token.
    struct token *next_server;
};

struct redisServer {
    // append new redisUnit to the end of list and keep `message_center`
    // time ordered
    struct list *message_center;
    struct array *instances;
    size_t max_id;
    size_t live_instances;

    size_t nbackup;
    struct token *tokens;
    size_t ntoken;
    long timeout;
};

struct redisInstance {
    size_t id;
    wstr ip;
    int port;
    time_t timeout_duration;
    // means the amount of timeout response under this instance
    int ntimeout;
    unsigned live:1;
    // When a instance keep timeout_duration larger than threshold value,
    // this instance is marked as `dirty`. Only when `instance->ntimeout`
    // recover to zero, `is_dirty` is cleared.
    // Read command will choose another non-dirty instance to send command
    unsigned is_dirty:1;

    struct client *redis_client;
    struct list *wait_units;
    size_t ntoken;
};

struct token *hashDispatch(struct redisServer *server, struct slice *key);
int hashInit(struct redisServer *server);
int initInstance(struct redisInstance *instance, size_t, wstr, int, int);

#endif
