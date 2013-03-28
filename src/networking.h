// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_NETWORKING_H
#define WHEATSERVER_NETWORKING_H

#include "wheatserver.h"

#define WHEAT_IOBUF_LEN 1024 * 4

struct masterClient;
int readBulkFrom(int fd, struct slice *slice);
int writeBulkTo(int fd, struct slice *clientbuf);
void replyMasterClient(struct masterClient *c, const char *buf, size_t len);
ssize_t isClientPreparedWrite(int fd, struct evcenter *center, void *c);
int syncWriteBulkTo(int fd, struct slice *slice);
int syncReadBulkFrom(int fd, struct slice *slice);

#endif
