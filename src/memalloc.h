// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_MEMALLOC
#define WHEATSERVER_MEMALLOC

void *wmalloc(size_t size);
void *wrealloc(void *old, size_t size);
void *wcalloc(size_t count, size_t size);
void wfree(void *ptr);

#endif
