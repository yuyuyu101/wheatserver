// A string implementation of no payload
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "slice.h"
#include "memalloc.h"
#include "debug.h"

struct slice *sliceCreate(uint8_t *data, size_t len)
{
    struct slice *b = wmalloc(sizeof(*b));
    if (data == NULL)
        b->len = 0;
    else
        b->len = len;
    b->data = data;
    return b;
}

void sliceTo(struct slice *s, uint8_t *data, size_t len)
{
    if (data == NULL)
        return ;
    s->len = len;
    s->data = data;
}

void sliceFree(struct slice *b)
{
    wfree(b);
}

void sliceClear(struct slice *b)
{
    b->len = 0;
    b->data = NULL;
}

void sliceRemvoePrefix(struct slice *b, size_t prefix)
{
    assert(prefix < b->len);
    b->data += prefix;
    b->len -= prefix;
}

int sliceStartWith(struct slice *b, struct slice *s)
{
    return ((b->len >= s->len) &&
            (memcmp(b->data, s->data, s->len) == 0));
}

int sliceCompare(struct slice *b, struct slice *s)
{
    const size_t min_len = (b->len < s->len) ? b->len : s->len;
    int r = memcmp(b->data, s->data, min_len);
    if (r == 0) {
        if (b->len < s->len)
            r = -1;
        else if
            (b->len > s->len) r = 1;
    }
    return r;
}

#ifdef SLICE_TEST_MAIN
#include <stdio.h>
#include "test_help.h"

int main(int argc, const char *argv[])
{
    {
        struct slice *s1 = sliceCreate((uint8_t *)"abcdefg", 7);
        struct slice *s2 = sliceCreate((uint8_t *)"abcd", 4);
        test_cond((uint8_t *)"sliceStartWith", sliceStartWith(s1, s2));
        sliceRemvoePrefix(s1, 2);
        test_cond("sliceStartWith not", !sliceStartWith(s1, s2));
        sliceClear(s1);
        sliceTo(s1, (uint8_t *)"abcd", 4);
        test_cond("sliceCompare", !sliceCompare(s1, s2));
    }
    test_report();
    return 0;
}
#endif

