// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdlib.h>
#include <assert.h>

#if 0
#include <stdio.h>
static int alloc[33000];
#define STAT_ALLOC(size) do { \
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1)); \
    alloc[size]++; \
}while(0)

void allocPrint()
{
    int i;
    for (i = 0; i < 33000; i++) {
        if (alloc[i] != 0)
            printf("size %d: %d times\n", i, alloc[i]);
    }
}
#else
#define STAT_ALLOC(size)
#endif

void *wmalloc(size_t size)
{
    STAT_ALLOC(size);
    void *p = malloc(size);
    return p;
}

void *wrealloc(void *old, size_t size)
{
    STAT_ALLOC(size);
    void *p = realloc(old, size);
    return p;
}

void *wcalloc(size_t count, size_t size)
{
    void *p = wmalloc(size*count);
    return p;
}

void wfree(void *ptr)
{
    free(ptr);
}


