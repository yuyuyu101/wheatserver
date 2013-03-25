// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_WORKER_MBUF_H
#define WHEATSERVER_WORKER_MBUF_H

struct msghdr;

struct msghdr *msgCreate();
void msgClean(struct msghdr *hdr);
// You must call msgSetReaded after msgRead
void msgRead(struct msghdr *hdr, struct slice *s);
void msgSetReaded(struct msghdr *hdr, size_t len);
// You must call msgSetWritted after msgPut
int msgPut(struct msghdr *hdr, struct slice *s);
void msgSetWritted(struct msghdr *hdr, size_t len);
void msgFree(struct msghdr *hdr);
size_t msgGetSize(struct msghdr *hdr);
int msgCanRead(struct msghdr *hdr);

#endif
