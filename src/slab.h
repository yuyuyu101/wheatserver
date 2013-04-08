// Implementation of memory management base on Wheatserver's features
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_SLAB_H
#define WHEATSERVER_SLAB_H

struct slabcenter;

// Note:
// Learn memory pool from Nginx and simplify it.
// It can reach rapid allocation and needn't free objects. Based on
// Wheatserver's request-response model, alloc a big buffer and
// distribute them, free all finally.

struct slabcenter *slabcenterCreate(const int item_max, const double factor);
void slabcenterDealloc(struct slabcenter *c);

void *slabAlloc(struct slabcenter *center, const size_t size);

#endif
