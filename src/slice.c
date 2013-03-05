#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "slice.h"

struct slice *sliceCreate(const char *data, size_t len)
{
    struct slice *b = malloc(sizeof(*b));
    if (data == NULL)
        b->len = 0;
    else
        b->len = len;
    b->data = data;
    return b;
}

void sliceTo(struct slice *s, const char *data, size_t len)
{
    if (data == NULL)
        return ;
    s->len = len;
    s->data = data;
}

void sliceFree(struct slice *b)
{
    free(b);
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
    const int min_len = (b->len < s->len) ? b->len : s->len;
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
        struct slice *s1 = sliceCreate("abcdefg", 7);
        struct slice *s2 = sliceCreate("abcd", 4);
        test_cond("sliceStartWith", sliceStartWith(s1, s2));
        sliceRemvoePrefix(s1, 2);
        test_cond("sliceStartWith not", !sliceStartWith(s1, s2));
        sliceClear(s1);
        sliceTo(s1, "abcd", 4);
        test_cond("sliceCompare", !sliceCompare(s1, s2));
    }
    test_report();
    return 0;
}
#endif

