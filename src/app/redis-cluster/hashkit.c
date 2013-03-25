#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "redis.h"

#define WHEAT_TOKEN_PER_SERVER   40
#define WHEAT_KEY_RANGE          UINT32_MAX
#define KETAMA_MAX_HOSTLEN       86

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

int hashUpdate(struct redisServer *server)
{
    ASSERT(server->instance_size >= server->nbackup && server->instance_size);
    struct token *tokens = server->tokens, *last_token = NULL;
    size_t ntoken = server->ntoken;
    size_t need_ntoken = server->instance_size*WHEAT_TOKEN_PER_SERVER;
    int i, j, swap, prev_idx;
    uint64_t range_per_token;
    if (ntoken < need_ntoken) {
        tokens = wrealloc(tokens, sizeof(struct token)*need_ntoken);
        if (!tokens)
            return WHEAT_WRONG;
        server->tokens = tokens;
        ntoken = need_ntoken;
    }

    range_per_token = WHEAT_KEY_RANGE / ntoken;

    for (i = 0, j = 0; i < ntoken; ++i, ++j) {
        tokens[i].next_server = NULL;
        tokens[i].value = (uint32_t)(i * range_per_token);
        tokens[i].server_idx = server->instances[i%server->instance_size].id;
    }

    if (server->ntoken == 0) {
        // shuffle
        for (i = 0; i < ntoken; i++)
        {
            size_t j = i + rand() / (RAND_MAX / (ntoken - i) + 1);
            swap = tokens[j].server_idx;
            tokens[j].server_idx = tokens[i].server_idx;
            tokens[i].server_idx = swap;
        }
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
    struct token *begin, *left, *end, *right, *middle;

    begin = left = server->tokens;
    end = right = server->tokens + server->ntoken - 1;

    while (left < right) {
        middle = left + (right - left) / 2;
        if (middle->value < hash) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    if (right == end) {
        right = begin;
    }
    ASSERT(right);
    return right;
}
