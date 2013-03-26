// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "redis.h"

#define WHEAT_KEYSPACE           1024

extern void md5_signature(const unsigned char *key, unsigned int length, unsigned char *result);

static uint32_t ketamaHash(const char *key, size_t key_length, uint32_t alignment)
{
    unsigned char results[16];

    md5_signature((unsigned char*)key, key_length, results);

    return ((uint32_t) (results[3 + alignment * 4] & 0xFF) << 24)
        | ((uint32_t) (results[2 + alignment * 4] & 0xFF) << 16)
        | ((uint32_t) (results[1 + alignment * 4] & 0xFF) << 8)
        | (results[0 + alignment * 4] & 0xFF);
}

// hashAdd is used when a new redis server add to rebalance tokens
int hashAdd(struct redisServer *server, wstr ip, int port, int id)
{
    size_t ninstance, ntoken_per_instance, extra_ntoken, target_idx, ntoken;
    struct token *token;
    struct redisInstance *instance, *target_instance;
    struct redisInstance add_instance;
    int *sub_ntoken;
    int i;

    initInstance(&add_instance, id, ip, port, DIRTY);
    arrayPush(server->instances, &add_instance);

    // `sub_ntoken`'s element is the amount of tokens every instance should
    // take out
    ninstance = narray(server->instances);
    sub_ntoken = wmalloc(sizeof(int)*ninstance);
    ntoken_per_instance = WHEAT_KEYSPACE / ninstance;
    extra_ntoken = WHEAT_KEYSPACE % ninstance;
    for (i = 0; i < ninstance; i++) {
        instance = arrayIndex(server->instances,i);
        sub_ntoken[i] = instance->ntoken - ntoken_per_instance;
    }
    if (!extra_ntoken) {
        for (i = 0; i < extra_ntoken; i++) {
            sub_ntoken[i]--;
        }
    }

    target_idx = ninstance-1;
    extra_ntoken = sub_ntoken[target_idx];
    token = &server->tokens[0];
    target_instance = arrayIndex(server->instances, target_idx);
    while (extra_ntoken) {
        int skip = random() % ntoken_per_instance + 1;
        while (skip) {
            token = token->next_server;
        }
        if (token->server_idx != target_idx && sub_ntoken[token->server_idx]) {
            instance = arrayIndex(server->instances, token->server_idx);
            token->server_idx = target_idx;
            instance->ntoken--;
            target_instance->ntoken++;
        }
    }

    ntoken = 0;
    for (i = 0; i < ninstance; i++) {
        instance = arrayIndex(server->instances,i);
        ntoken += instance->ntoken;
    }
    ASSERT(ntoken == WHEAT_KEYSPACE);

    wfree(sub_ntoken);
    return WHEAT_OK;
}

int hashInit(struct redisServer *server)
{
    ASSERT(narray(server->instances) >= server->nbackup);
    struct token *tokens = server->tokens, *last_token = NULL;
    struct redisInstance *instance;
    size_t ntoken, ninstance;
    int i, swap, prev_idx;

    ntoken = WHEAT_KEYSPACE;
    tokens = wmalloc(sizeof(struct token)*ntoken);
    if (!tokens)
        return WHEAT_WRONG;
    server->tokens = tokens;

    ninstance = narray(server->instances);
    for (i = 0; i < ntoken; ++i) {
        instance = arrayIndex(server->instances, i%ninstance);
        tokens[i].next_server = NULL;
        tokens[i].value = i;
        tokens[i].server_idx = i%ninstance;
        instance->ntoken++;
    }

    // shuffle
    for (i = 0; i < ntoken; i++)
    {
        size_t j = i + rand() / (RAND_MAX / (ntoken - i) + 1);
        swap = tokens[j].server_idx;
        tokens[j].server_idx = tokens[i].server_idx;
        tokens[i].server_idx = swap;
    }

    // Populate each token.next_server to quicker search node process
    prev_idx = 0;
    i = 1;
    last_token = &tokens[ntoken-1];
    while (last_token->next_server == NULL) {
        if (tokens[i%ntoken].server_idx != tokens[prev_idx].server_idx) {
            for (; prev_idx < i && prev_idx < ntoken; ++prev_idx)
                tokens[prev_idx].next_server = &tokens[i%ntoken];
        }
        ++i;
    }
    server->ntoken = ntoken;
    return WHEAT_OK;
}

struct token *hashDispatch(struct redisServer *server, struct slice *key)
{
    uint32_t hash = ketamaHash((const char *)key->data, key->len, 0);

    return &server->tokens[hash%WHEAT_KEYSPACE];
}
