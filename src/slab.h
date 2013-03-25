// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_SLAB_H
#define WHEATSERVER_SLAB_H

struct slabcenter;
struct slabcenter *slabcenterCreate(const int item_max, const double factor);
void slabcenterDealloc(struct slabcenter *c);

void *slabAlloc(struct slabcenter *center, const size_t size);

#endif
