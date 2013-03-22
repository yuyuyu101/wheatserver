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
    size_t nreader;
    size_t nwriter;
    size_t nbackup;
    struct redisConnUnit **reserved;
    struct token *tokens;
    size_t ntoken;
    struct dict *message_center;
};

struct redisInstance {
    int id;
    wstr ip;
    int port;

    struct array *server_conns;
    struct list *free_conns;
};

struct token *hashDispatch(struct redisServer *server, struct slice *key);
int hashUpdate(struct redisServer *server);

#endif
