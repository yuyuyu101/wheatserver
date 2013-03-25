// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H
#define WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H

#include "../application.h"

struct token {
    uint32_t value;
    int server_idx;
    struct token *next_server;
};

struct redisServer {
    struct redisInstance *instances;
    size_t instance_size;
    size_t live_instances;

    size_t nbackup;
    struct token *tokens;
    size_t ntoken;
    long timeout;
    // append new redisUnit to the end of list and keep `message_center`
    // time ordered
    struct list *message_center;
};

struct redisInstance {
    int id;
    wstr ip;
    int port;
    // means the amount of timeout response under this instance
    int ntimeout;
    unsigned live:1;

    struct client *redis_client;
    struct list *wait_units;
};

struct token *hashDispatch(struct redisServer *server, struct slice *key);
int hashUpdate(struct redisServer *server);

#endif
