// mbuf structure implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_WORKER_MBUF_H
#define WHEATSERVER_WORKER_MBUF_H

struct msghdr;

// mbuf is a structure used to support no copy demand and learn from BSD kernel.
// Wheatserver uses mbuf as send and receive buffer to store.
//
// Use cases:
//     struct msghdr *req_buf = msgCreate();
//     sturct slice slice;
//     msgPut(req_buf, &slice);
//     n = read(fd, slice.data, slice.len);
//     msgSetWritted(req_buf, n);
//     ...
//     msgRead(req_buf, &slice);
//     int nparsed = parser(&slice);
//     msgSetReaded(req_buf, nparsed);

struct msghdr *msgCreate();
void msgClean(struct msghdr *hdr);
// You must call msgSetReaded after msgRead
void msgRead(struct msghdr *hdr, struct slice *s);
void msgSetReaded(struct msghdr *hdr, size_t len);
// You must call msgSetWritted after msgPut
int msgPut(struct msghdr *hdr, struct slice *s);
void msgSetWritted(struct msghdr *hdr, size_t len);
void msgFree(struct msghdr *hdr);
// Get total size of all mbuf in `hdr`
size_t msgGetSize(struct msghdr *hdr);
// Check if can get unread content from `hdr`
int msgCanRead(struct msghdr *hdr);

#endif
