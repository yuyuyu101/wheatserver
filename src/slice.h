// A string implementation of no payload
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_SLICES_H
#define WHEATSERVER_SLICES_H

#include <stdint.h>

// Learn from LevelDB, using slice in Wheatserver everywhere(Like it very much).
//
// Typical case:
// 1.
//     char buf[1024];
//     struct slice slice;
//     sliceTo(&slice, buf, 1024);
//     readBulkFrom(fd, &slice);
// 2.
//     char packet[4096];
//     ...
//     struct slice field, value;
//     parsePacket(packet, &field, &value);

struct slice {
    uint8_t *data;
    size_t len;
};

struct slice *sliceCreate(uint8_t *data, size_t len);
void sliceTo(struct slice *s, uint8_t *data, size_t len);
void sliceFree(struct slice *b);
void sliceClear(struct slice *b);
void sliceRemvoePrefix(struct slice *b, size_t prefix);
int sliceStartWith(struct slice *b, struct slice *s);
int sliceCompare(struct slice *b, struct slice *s);

#endif
