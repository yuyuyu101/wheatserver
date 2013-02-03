#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>

#include "dict.h"

/* ========== private functions ==========*/

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(struct dict *d)
{
    /* If the hash table is empty expand it to the intial size. */
    if (d->size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    if (d->used >= d->size && d->used/d->size > DICT_FORCE_RESIZE_RATIO)
    {
        return dictExpand(d, d->used*2);
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

static int _dictKeyIndex(struct dict *d, const void *key)
{
    unsigned int h, idx;
    struct dictEntry *he;

    /* Expand the hash table if needed */
    if (_dictExpandIfNeeded(d) == DICT_WRONG)
        return -1;
    /* Compute the key hash value */
    h = dictHashKey(d, key);
    idx = h & d->sizemask;
    /* Search if this slot does not already contain the given key */
    he = d->table[idx];
    while(he) {
        if (dictCompareKeys(d, key, he->key))
            return -1;
        he = he->next;
    }
    return idx;
}

/* ========== public functions ==========*/

struct dict *dictCreate(struct dictType *type)
{
    struct dict *d = malloc(sizeof(struct dict));
    d->size = 0;
    d->sizemask = 0;
    d->used = 0;
    d->type = type;
    d->table = NULL;
    _dictExpandIfNeeded(d);
    return d;
}

int dictExpand(struct dict *d, unsigned long size)
{
    unsigned long realsize = _dictNextPower(size);

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table */
    if (d->used > size)
        return DICT_WRONG;

    /* Allocate the new hash table and initialize all pointers to NULL */
    d->size = realsize;
    d->sizemask = realsize-1;
    if (d->table)
        d->table = realloc(d->table, realsize*sizeof(struct dictEntry*));
    else
        d->table = malloc(realsize*sizeof(struct dictEntry*));
    memset(d->table, 0, realsize*sizeof(struct dictEntry*));

    return DICT_OK;
}

struct dictEntry *dictAddRaw(struct dict *d, void *key)
{
    int index;
    struct dictEntry *entry;

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    if ((index = _dictKeyIndex(d, key)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry */
    entry = malloc(sizeof(struct dictEntry));
    memset(entry, 0, sizeof(*entry));
    entry->next = d->table[index];
    d->table[index] = entry;
    d->used++;

    /* Set the hash entry fields. */
    dictSetKey(d, entry, key);
    return entry;
}

int dictAdd(struct dict *d, void *key, void *val)
{
    struct dictEntry *entry = dictAddRaw(d,key);

    if (!entry) return DICT_WRONG;
    dictSetVal(d, entry, val);
    return DICT_OK;
}

struct dictEntry *dictReplaceRaw(struct dict *d, void *key)
{
    struct dictEntry *entry = dictFind(d,key);

    return entry ? entry : dictAddRaw(d,key);
}

int dictReplace(struct dict *d, void *key, void *val, int *replace)
{
    struct dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */
    if (dictAdd(d, key, val) == DICT_OK) {
        if (replace)
            *replace = 0;
        return DICT_OK;
    }
    /* It already exists, get the entry */
    entry = dictFind(d, key);
    if (entry == NULL) {
        return DICT_WRONG;
    }
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    auxentry = *entry;
    dictSetVal(d, entry, val);
    dictFreeVal(d, &auxentry);
    if (replace)
        *replace = 1;
    return DICT_OK;
}

struct dictEntry *dictFind(struct dict *d, const void *key)
{
    struct dictEntry *he;
    unsigned int h, idx;

    if (d->size == 0) return NULL;
    h = dictHashKey(d, key);
    idx = h & d->sizemask;
    he = d->table[idx];
    while(he) {
        if (dictCompareKeys(d, key, he->key))
            return he;
        he = he->next;
    }
    return NULL;
}

/* Search and remove an element */
static int dictGenericDelete(struct dict *d, const void *key, int nofree)
{
    unsigned int h, idx;
    struct dictEntry *he, *prevHe;

    if (d->size == 0) return DICT_WRONG;
    h = dictHashKey(d, key);

    idx = h & d->sizemask;
    he = d->table[idx];
    prevHe = NULL;
    while(he) {
        if (dictCompareKeys(d, key, he->key)) {
            /* Unlink the element from the list */
            if (prevHe)
                prevHe->next = he->next;
            else
                d->table[idx] = he->next;
            if (!nofree) {
                dictFreeKey(d, he);
                dictFreeVal(d, he);
            }
            free(he);
            d->used--;
            return DICT_OK;
        }
        prevHe = he;
        he = he->next;
    }
    return DICT_WRONG; /* not found */
}

int dictDelete(struct dict *ht, const void *key) {
    return dictGenericDelete(ht, key, 0);
}

int dictDeleteNoFree(struct dict *ht, const void *key) {
    return dictGenericDelete(ht, key, 1);
}

void dictClear(struct dict *d)
{
    unsigned long i;
    /* Free all the elements */
    for (i = 0; i < d->size && d->used > 0; i++) {
        struct dictEntry *he, *nextHe;

        if ((he = d->table[i]) == NULL) continue;
        while(he) {
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            free(he);
            d->used--;
            he = nextHe;
        }
    }
    free(d->table);
    d->size = 0;
    d->sizemask = 0;
    d->used = 0;
    d->table = NULL;
}

void dictRelease(struct dict *d)
{
    dictClear(d);
    free(d);
}

void *dictFetchValue(struct dict *d, const void *key)
{
    struct dictEntry *he;

    he = dictFind(d, key);
    return he ? dictGetVal(he) : NULL;
}

struct dictIterator *dictGetIterator(struct dict *d)
{
    struct dictIterator *iter = malloc(sizeof(struct dictIterator));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

struct dictEntry *dictNext(struct dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {
            struct dict *d = iter->d;
            iter->index++;
            if (iter->index >= (signed) d->size) {
                break;
            }
            iter->entry = d->table[iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;

}

void dictReleaseIterator(struct dictIterator *iter)
{
    free(iter);
}

static uint32_t dict_hash_function_seed = 2128;

/* MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

/* And a case insensitive hash function (based on djb hash) */
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}

void dictPrint(struct dict *d)
{
    struct dictIterator *iter = dictGetIterator(d);
    struct dictEntry *entry;
    while ((entry = dictNext(iter)) != NULL) {
        printf("%s -> %s \n", (char *)entry->key, (char *)entry->v.val);
    }
    dictReleaseIterator(iter);
}

#define DICT_STATS_VECTLEN 50
void dictPrintStats(struct dict *d) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (d->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < d->size; i++) {
        struct dictEntry *he;

        if (d->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = d->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", d->size);
    printf(" number of elements: %ld\n", d->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)d->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/d->size)*100);
    }
}

#ifdef DICT_TEST_MAIN
#include "test_help.h"
#include "wstr.h"

unsigned int dictWstrHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, wstrlen((char*)key));
}

unsigned int dictWstrCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, wstrlen((char*)key));
}

int dictWstrKeyCompare(const void *key1,
        const void *key2)
{
    int l1,l2;

    l1 = wstrlen((wstr)key1);
    l2 = wstrlen((wstr)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictWstrDestructor(void *val)
{
    wstrFree(val);
}

struct dictType wstrDictType = {
    dictWstrHash,               /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictWstrKeyCompare,         /* key compare */
    dictWstrDestructor,         /* key destructor */
    dictWstrDestructor          /* val destructor */
};

int main(void)
{
    {
        int replace;
        struct dict *d = dictCreate(&wstrDictType);
        test_cond("dictCreate()", dictSize(d) == 0);
        test_cond("dictAdd()", dictAdd(d, wstrNew("key1"), wstrNew("value1")) == DICT_OK);
        test_cond("dictAdd()", dictAdd(d, wstrNew("key2"), wstrNew("value2")) == DICT_OK);
        test_cond("dictSize()", dictSize(d) == 2);
        test_cond("dictSlots()", dictSlots(d) == 16);
        test_cond("dictReplace() exists", dictReplace(d, wstrNew("key1"), wstrNew("replace"), &replace) == DICT_OK);
        struct dictEntry *entry = dictFind(d, wstrNew("key1"));
        wstr x = entry->v.val;
        test_cond("dictFind()", memcmp(x, "replace", 7) == DICT_OK);
        test_cond("dictReplace() not exists", dictReplace(d, wstrNew("key3"), wstrNew("value3"), &replace) == DICT_OK);
        test_cond("dictFetchValue()", memcmp(dictFetchValue(d, wstrNew("key3")), wstrNew("value3"), 6) == 0);
        test_cond("dictDelete()", dictDelete(d, wstrNew("key3")) == DICT_OK);
        entry = dictFind(d, wstrNew("key3"));
        test_cond("dictFind()", entry == NULL);

        struct dictIterator *iter = dictGetIterator(d);
        int i = 0;
        while((entry = dictNext(iter)) != NULL) {
            i++;
        }
        test_cond("iterator", i == 2);
        dictReleaseIterator(iter);
        dictRelease(d);
    }
    test_report();
}
#endif
