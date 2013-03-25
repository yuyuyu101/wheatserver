// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_ARRAY_H
#define WHEATSERVER_ARRAY_H

struct array;
struct array *arrayCreate(size_t elem_size, size_t cap);
void arrayDealloc(struct array *arr);
void *arrayIndex(struct array *a, size_t pos);
void arraySet(struct array *a, void *data, size_t p);
void arrayPush(struct array *a, void *data);
void *arrayPop(struct array *a);
void *arrayTop(struct array *a);
void arrayEach(struct array *a, void(*func)(void *));
void arrayEach2(struct array *a, void(*func)(void *, void *), void *data);

size_t narray(struct array *a);

#endif
