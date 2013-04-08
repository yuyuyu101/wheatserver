// wstr, A C dynamic strings library, referenced from Redis
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_WSTR_H
#define WHEATSERVER_WSTR_H

#include <stddef.h>

#define MAX_STR 16*1024*1024

// 1. Return NULL means call func failed but the old value passed in stay
// alive.
//
// 2. `len` in struct wstrhd stand for the length of string.

typedef char *wstr;

// wstr memory format:
//     | len | free | xxxxxxxxxxxxxx           |
//                   |              |          |
//                  buf             |          |
//                   |              |          |
//                   <------len----->          |
//                   |              <---free--->
// wstr put `len` and `free` fields into header bytes and return pointer points
// to buf.
// Advantage:
//     * compatible with methods in <string.h>
//     * avoid destroy header because of buffer overflow
//
// `len`: reprensented for the length of `buf`
// `free`: reprensented for the free length of `buf`

struct wstrhd {
    int len;
    int free;
    char buf[];
};

static inline int wstrlen(const wstr s)
{
    struct wstrhd *hd = (struct wstrhd *)(s - sizeof(struct wstrhd));
    return hd->len;
}

static inline int wstrfree(const wstr s)
{
    struct wstrhd *hd = (struct wstrhd *)(s - sizeof(struct wstrhd));
    return hd->free;
}

// wstrupdatelen is used to handle scene below:
//     int ret;
//     wstr str = wstrNewLen(NULL, 100);
//     ret = read(fd, str, 100);
//     wstrupdatelen(str, ret);
// As you can see, when expand or trunct buffer, we should modify wstrhd header
// to suite buffer by hand.
static inline void wstrupdatelen(wstr s, int len)
{
    struct wstrhd *hd = (struct wstrhd *)(s - sizeof(struct wstrhd));
    hd->free += (hd->len - len);
    hd->len = len;
    hd->buf[len] = '\0';
}

// If `init` is not NULL, create wstr and copy `init_len` length from `init`
// into wstr. wstrlen(wstr) == initlen && wstrfree(wstr) == 0
// If `init` is NULL and init_len is not zero, create `init_len` length buffer
// and set set free `init_len`
wstr wstrNewLen(const void *init, int init_len);
wstr wstrNew(const void *init);
wstr wstrEmpty();
void wstrFree(wstr s);
// Use case:
//     wstr s = wstrNew("1 2 3 4 5 6");
//     int count;
//     wstr *segements = wstrNewSplit(s, " ", 1, &count);
//     assert(count == 6);
//     assert(strcmp(segements[0], "1") == 0)
//     assert(strcmp(segements[5], "6") == 0)
//     ....
//     wstrFreeSplit(segements, count);
// More example and special cases you can see unit tests in wstr.c
wstr *wstrNewSplit(wstr s, const char *sep, int sep_len, int *count);
void wstrFreeSplit(wstr *slots, int count);
wstr wstrDup(const wstr s);
int wstrCmp(const wstr s1, const wstr s2);
int wstrCmpChars(const wstr s1, const char *s2, size_t len);
int wstrCmpNocaseChars(const wstr s1, const char *s2, size_t l2);
void wstrLower(wstr s);
void wstrUpper(wstr s);
int wstrIndex(wstr s, const char t);
int wstrStartWithChars(const wstr s, const char *s2, size_t len);
int wstrStartWith(const wstr s, const wstr s2);

wstr wstrCat(wstr s, const char *t);
wstr wstrCatLen(wstr s, const char *t, size_t len);
wstr wstrRange(wstr, int left, int right);
wstr wstrStrip(wstr, const char *chars);
wstr wstrRemoveFreeSpace(wstr);
void wstrClear(wstr);

// Low function
// wstrMakeRoom is used to make more space room to store when meet large bulk
// buffer.
// Case:
//     int ret;
//     wstr str = wstrNewLen(NULL, 100);
//     ret = read(fd, str, 100);
//     wstrupdatelen(str, ret);
//     ....
//     str = wstrMakeRoom(str, 1000);
//     ret = read(fd, str+wstr(len), wstrfree(str));
//     wstrupdatelen(str, ret);
wstr wstrMakeRoom(wstr s, size_t add_size);

#endif
