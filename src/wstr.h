#ifndef WHEATSERVER_WSTR_H
#define WHEATSERVER_WSTR_H

#include <stddef.h>

#define MAX_STR 16*1024*1024

/* 1. Return NULL means call func failed but the old value passed in stay
 * alive.
 *
 * 2. `len` in struct wstrhd stand for the length of string.
 * */

typedef char *wstr;

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

static inline void wstrupdatelen(wstr s, int len)
{
    struct wstrhd *hd = (struct wstrhd *)(s - sizeof(struct wstrhd));
    hd->free += (hd->len - len);
    hd->len = len;
    hd->buf[len] = '\0';
}

wstr wstrNewLen(const void *init, int init_len);
wstr wstrNew(const void *init);
wstr wstrEmpty();
void wstrFree(wstr s);
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

/* May modify exists wstr */
wstr wstrCat(wstr s, const char *t);
wstr wstrCatLen(wstr s, const char *t, size_t len);
wstr wstrRange(wstr, int left, int right);
wstr wstrStrip(wstr, const char *chars);
wstr wstrRemoveFreeSpace(wstr);
void wstrClear(wstr);

/* low function */
wstr wstrMakeRoom(wstr s, size_t add_size);

#endif
