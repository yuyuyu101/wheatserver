// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H
#define WHEATSERVER_APP_REDIS_CLUSTER_REDIS_H

#include "../application.h"
#include "../../protocol/redis/proto_redis.h"

#define WHEAT_KEYSPACE                1024
#define WHEAT_SERVE_WAIT_MILLISECONDS 100

#define DIRTY    1
#define NONDIRTY 0

// struct token like virtual node in consistent hash
struct token {
    size_t pos;
    // `server_idx` is simply the offset of redisServer.instances
    size_t instance_id;
    // `next_instance` indicates the next real server token not virtual token.
    size_t next_instance;
};

struct configServer;

struct redisServer {
    size_t max_id;
    size_t nbackup;
    long timeout;

    struct configServer *config_server;
    size_t live_instances;
    // append new redisUnit to the end of list and keep `message_center`
    // time ordered. `message_center` is the owener of redisUnit, so you
    // have responsibility to free it
    struct list *message_center;
    // defer some connections, and now when outer connections comes and
    // server in config, we will append connection to pending_conns.
    struct list *pending_conns;
    struct array *instances;
    struct token *tokens;
    // Now must be WHEAT_KEYSPACE
    size_t ntoken;
    int is_serve;
};

struct redisInstance {
    // Unique identification ID, we use this id to track data owner
    size_t id;
    wstr ip;
    int port;

    struct list *wait_units;
    size_t ntoken;

    // Below fields all about the connection between redis and client,
    // it will lose effect when connection failed.
    struct client *redis_client;
    time_t timeout_duration;
    // means the amount of timeout response under this instance
    int ntimeout;
    // reliability is used to choose read instance who's reliability is
    // max. And in another condition, when write command responses are
    // different select max reliability instance from responses.
    // `reliability` is affected by timeout, lose connection times
    int reliability;
    unsigned live:1;
    // When a instance keep timeout_duration larger than threshold value,
    // this instance is marked as `dirty`. Only when `instance->ntimeout`
    // recover to zero, `is_dirty` is cleared.
    // Read command will choose another non-dirty instance to send command
    unsigned is_dirty:1;
};

struct token *hashDispatch(struct redisServer *server, struct slice *key);
int hashInit(struct redisServer *server);
int initInstance(struct redisInstance *instance, size_t, wstr, int, int);

struct configServer *configServerCreate(struct client *client, int use_redis);
void configServerDealloc(struct configServer *config_server);
int getServerFromRedis(struct redisServer *);
int configFromFile(struct redisServer *server);
int handleConfig(struct redisServer *server, struct conn *c);
int isStartServe(struct redisServer *server);

#endif
