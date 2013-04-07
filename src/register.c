// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "wheatserver.h"

extern struct worker SyncWorker;
extern struct worker AsyncWorker;

struct worker *WorkerTable[] = {
    &SyncWorker,
    &AsyncWorker,
    NULL,
};

extern struct protocol ProtocolRedis;
extern struct protocol ProtocolHttp;
struct protocol *ProtocolTable[] = {
    &ProtocolHttp,
    &ProtocolRedis,
    NULL,
};

extern struct app AppWsgi;
extern struct app AppRedis;
extern struct app AppStatic;

struct app *AppTable[] = {
    &AppWsgi,
    &AppStatic,
    &AppRedis,
    NULL,
};
