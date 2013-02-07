#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "wstr.h"

wstr wstrNewLen(const void *init, int init_len)
{
    struct wstrhd *sh;
    sh = malloc(sizeof(struct wstrhd)+init_len+1);
    if (sh == NULL) {
        return NULL;
    }
    sh->len = init_len;
    sh->free = 0;
    if (init_len && init) {
        memcpy(sh->buf, init, init_len);
    }
    sh->buf[init_len] = '\0';
    return (wstr)(sh->buf);
}

wstr wstrNew(const void *init)
{
    size_t len = (init == NULL) ? 0: strlen(init);
    return wstrNewLen(init, (int)len);
}

wstr wstrEmpty()
{
    return wstrNewLen(NULL, 0);
}

wstr wstrMakeRoom(wstr s, size_t add_size)
{
    struct wstrhd *sh = (void *)(s - sizeof(struct wstrhd));
    if (sh->free > add_size)
        return s;

    int old_len = wstrlen(s);
    int new_len = (old_len + (int)add_size) * 2;
    if (new_len > MAX_STR)
        return s;
    struct wstrhd *new_sh = realloc(sh, sizeof(struct wstrhd)+new_len+1); // +1 for '\0'
    if (new_sh == NULL)
        return NULL;
    new_sh->free = new_len - old_len;
    return new_sh->buf;
}

void wstrFree(wstr s)
{
    if (s == NULL) {
        return ;
    }
    free(s - sizeof(struct wstrhd));
}

wstr wstrDup(const wstr s)
{
    return wstrNewLen(s, wstrlen(s));
}

wstr *wstrNewSplit(wstr s, const char *sep, int sep_len, int *count)
{
    int slots_count = 5, s_len = wstrlen(s);
    wstr *slots;
    slots = malloc(sizeof(wstr*)*slots_count);

    wstr fixed = s;
    int fixed_i = 0, c = 0, i;
    for (i = 0; i < s_len; i++) {
        if (sep[0] == s[i] && memcmp(sep, &s[i], sep_len) == 0) {
            slots[c] = wstrNewLen(fixed, i-fixed_i);
            if (slots[c] == NULL)
                goto cleanup;
            c++;
            fixed = &s[i] + sep_len;
            fixed_i = i + sep_len;
            i += (sep_len - 1);
        }
        if (c > slots_count - 1) {
            wstr *new_slots;
            slots_count *= 2;
            new_slots = realloc(slots, sizeof(wstr*)*slots_count);
            if (new_slots == NULL)
                goto cleanup;
            slots = new_slots;
        }
    }
    slots[c] = wstrNewLen(fixed, s_len-fixed_i);
    if (slots[c] == NULL)
        goto cleanup;
    *count = ++c;
    return slots;

cleanup:
    {
        int j;
        for (j = 0; j < c; j++) {
            wstrFree(slots[j]);
        }
        free(slots);
        *count = 0;
        return NULL;
    }
}

void wstrFreeSplit(wstr *slots, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        wstrFree(slots[i]);
    }
    free(slots);
}

int wstrCmp(const wstr s1, const wstr s2)
{
    int l1, l2, minlen;
    int cmp;
    l1 = wstrlen(s1);
    l2 = wstrlen(s2);
    minlen = l1 < l2 ? l1: l2;

    cmp = memcmp(s1, s2, minlen);
    if (cmp == 0)
        return l1 - l2;
    return cmp;
}

int wstrCmpChars(const wstr s1, const char *s2, size_t len)
{
    wstr s = wstrNewLen(s2, (int)len);
    if (s == NULL)
        return 1;
    int ret = wstrCmp(s1, s);
    wstrFree(s);
    return ret;
}

void wstrLower(wstr s) {
    int len = wstrlen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

void wstrUpper(wstr s) {
    int len = wstrlen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}

wstr wstrCatLen(wstr s, const char *t, size_t tlen)
{
    if (s == NULL || t == NULL)
        return NULL;
    int slen;
    slen = wstrlen(s);
    wstr new_s;
    new_s = wstrMakeRoom(s, tlen);
    if (new_s == NULL)
        return NULL;
    memcpy(new_s+slen, t, tlen);
    wstrupdatelen(new_s, (int)tlen+slen);
    return new_s;

}

wstr wstrCat(wstr s, const char *t)
{
    return wstrCatLen(s, t, strlen(t));
}

wstr wstrRange(wstr str, int left, int right)
{
    if (str == NULL)
        return NULL;

    int len = wstrlen(str);

    //process negative
    if (left < 0) {
        left = len + left;
    }
    if (right < 0) {
        right = len + right;
    }
    else if (right == 0)
        right = len;

    //process out of index
    if (left > len)
        left = len;
    if (right > len)
        right = len;

    int new_len = (right - left);
    if (new_len < 0)
        new_len = 0;

    memmove(str, str+left, new_len);
    wstrupdatelen(str, new_len);
    return str;
}

wstr wstrStrip(wstr str, const char *chars)
{
    int left, right, len = wstrlen(str);

    left = 0;
    right = len - 1;
    while(left < len && strchr(chars, str[left])) left++;
    while(right > left && strchr(chars, str[right])) right--;
    memmove(str, str+left, right-left+1);

    wstrupdatelen(str, right-left+1);
    return str;
}

#ifdef WSTR_TEST_MAIN
#include "test_help.h"
#include <stdio.h>

int main(void) {
    {
        struct wstrhd *sh;
        wstr x = wstrNew("foo"), y;

        test_cond("Create a string and obtain the length",
            wstrlen(x) == 3 && memcmp(x,"foo\0",4) == 0);

        wstrFree(x);
        x = wstrNewLen("foo",2);
        test_cond("Create a string with specified length",
            wstrlen(x) == 2 && memcmp(x,"fo\0",3) == 0);

        x = wstrCat(x,"bar");
        test_cond("Strings concatenation",
            wstrlen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

//        x = wstrcpy(x,"a");
//        test_cond("wstrcpy() against an originally longer string",
//            wstrlen(x) == 1 && memcmp(x,"a\0",2) == 0)
//
//        x = wstrcpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
//        test_cond("wstrcpy() against an originally shorter string",
//            wstrlen(x) == 33 &&
//            memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

//        wstrfree(x);
//        x = wstrcatprintf(wstrempty(),"%d",123);
//        test_cond("wstrcatprintf() seems working in the base case",
//            wstrlen(x) == 3 && memcmp(x,"123\0",4) ==0)
//

        y = wstrRange(wstrDup(x),1,2);
        test_cond("wstrrange(...,1,2)",
            wstrlen(y) == 1 && wstrfree(y) == 4 && memcmp(y,"o\0",2) == 0);

        wstrFree(y);
        y = wstrRange(wstrDup(x),1,-1);
        test_cond("wstrrange(...,1,-1)",
            wstrlen(y) == 3 && wstrfree(y) == 2 && memcmp(y,"oba\0",4) == 0);

        wstrFree(y);
        y = wstrRange(wstrDup(x),-2,-1);
        test_cond("wstrrange(...,-2,-1)",
            wstrlen(y) == 1 && wstrfree(y) == 4 && memcmp(y,"a\0",2) == 0);

        wstrFree(y);
        y = wstrRange(wstrDup(x),2,1);
        test_cond("wstrrange(...,2,1)",
            wstrlen(y) == 0 && wstrfree(y) == 5 && memcmp(y,"\0",1) == 0);

        wstrFree(y);
        y = wstrRange(wstrDup(x),1,100);
        test_cond("wstrrange(...,1,100)",
            wstrlen(y) == 4 && wstrfree(y) == 1 && memcmp(y,"obar\0",5) == 0);

        wstrFree(y);
        y = wstrRange(wstrDup(x),100,100);
        test_cond("wstrrange(...,100,100)",
            wstrlen(y) == 0 && wstrfree(y) == 5 && memcmp(y,"\0",1) == 0);

        wstrFree(y);
        wstrFree(x);

        x = wstrNew("foo");
        y = wstrNew("foa");
        test_cond("wstrcmp(foo,foa)", wstrCmp(x,y) > 0);

        wstrFree(y);
        wstrFree(x);

        x = wstrNew("bar");
        y = wstrNew("bar");
        test_cond("wstrcmp(bar,bar)", wstrCmp(x,y) == 0);

        wstrFree(y);
        wstrFree(x);

        x = wstrNew("aar");
        y = wstrNew("bar");
        test_cond("wstrcmp(bar,bar)", wstrCmp(x,y) < 0);

        wstrFree(x);
        wstrFree(y);

        x = wstrEmpty();
        test_cond("wstrEmpty()", wstrlen(x) == 0);
        wstrFree(x);

        x = wstrStrip(wstrNew("xxciaoyyy"),"xy");
        test_cond("wstrStrip() correctly trims characters",
            wstrlen(x) == 4 && memcmp(x,"ciao\0",5) == 0);
        wstrFree(x);

        x = wstrStrip(wstrNew(""),"xy");
        test_cond("wstrStrip() correctly trims characters",
            wstrlen(x) == 0 && memcmp(x,"\0",1) == 0);
        wstrFree(x);

        x = wstrStrip(wstrNew("xyxyxxyy"),"xy");
        test_cond("wstrStrip() correctly trims characters",
            wstrlen(x) == 0 && memcmp(x,"\0",1) == 0);
        wstrFree(x);

        x = wstrNewLen("port 10000", 10);
        int count;
        wstr *lines = wstrNewSplit(x, " ", 1, &count);
        test_cond("wstrNewSplit() split 'port 10000'",
                count == 2 && memcmp(lines[0], "port\0", 5) == 0
                && memcmp(lines[1], "10000\0", 6) == 0);
        {
            int oldfree;

            x = wstrNew("0");
            sh = (void*) (x-(sizeof(struct wstrhd)));
            test_cond("wstrNew() len buffers", sh->len == 1 && sh->free == 0);
            x = wstrMakeRoom(x, 1);
            sh = (void*) (x-(sizeof(struct wstrhd)));
            test_cond("wstrMakeRoom()", sh->len == 1 && sh->free == 3);
        }
    }
    test_report();
    return 0;
}
#endif
