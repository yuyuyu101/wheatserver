// Redis protocol parse implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_PROTOCOL_REDIS_PROTO_REDIS_H
#define WHEATSERVER_PROTOCOL_REDIS_PROTO_REDIS_H

// Protocol Redis API
struct slice *redisBodyNext(struct conn *c);
void redisBodyStart(struct conn*c);
void getRedisKey(struct conn *c, struct slice *out);
void getRedisCommand(struct conn *c, struct slice *out);
int getRedisArgs(struct conn *c);
int isReadCommand(struct conn*);
size_t getRedisKeyEndPos(struct conn *c);

#endif
