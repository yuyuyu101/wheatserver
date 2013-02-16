#ifndef WHEATSERVER_DICT_H
#define WHEATSERVER_DICT_H

#define DICT_OK 0
#define DICT_WRONG 1

#define DICT_HT_INITIAL_SIZE 16
#define DICT_FORCE_RESIZE_RATIO 5

#include <stdint.h>

struct dictEntry {
    void *key;
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
    } v;
    struct dictEntry *next;
};

struct dictType {
    unsigned int (*hashFunction)(const void *key);
    void *(*keyDup)(const void *key);
    void *(*valDup)(const void *obj);
    int (*keyCompare)(const void *key1, const void *key2);
    void (*keyDestructor)(void *key);
    void (*valDestructor)(void *obj);
};

struct dict {
    struct dictEntry **table;
    unsigned long size;
    unsigned long sizemask;
    unsigned long used;
    struct dictType *type;
};

struct dictIterator {
    struct dict *d;
    int table, index;
    struct dictEntry *entry, *nextEntry;
};

/* ========== Macros ========== */

#define dictOwnKey(d) ((d)->type->keyDup != NULL)
#define dictOwnValue(d) ((d)->type->valDup != NULL)

#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor && (entry)->v.val) \
        (d)->type->valDestructor((entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup(_val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup(_key_); \
    else \
        entry->key = (_key_); \
} while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor && entry->key) \
        (d)->type->keyDestructor((entry)->key)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare(key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictSlots(d) ((d)->size)
#define dictSize(d) ((d)->used)

/* Base Dict API */
struct dict *dictCreate(struct dictType*);
int dictExpand(struct dict *d, unsigned long size);
int dictAdd(struct dict *d, void *key, void *val);
struct dictEntry *dictAddRaw(struct dict *d, void *key);
int dictReplace(struct dict *d, void *key, void *val, int *replace);
struct dictEntry *dictReplaceRaw(struct dict *d, void *key);
int dictDelete(struct dict *d, const void *key);
int dictDeleteNoFree(struct dict *d, const void *key);
void dictRelease(struct dict *d);
struct dictEntry * dictFind(struct dict *d, const void *key);
void *dictFetchValue(struct dict *d, const void *key);
struct dictIterator *dictGetIterator(struct dict *d);
struct dictEntry *dictNext(struct dictIterator *iter);
void dictReleaseIterator(struct dictIterator *iter);
void dictPrintStats(struct dict *d);
void dictPrint(struct dict *d);
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictClear(struct dict *d);
void dictSetHashFunctionSeed(unsigned int initval);
unsigned int dictGetHashFunctionSeed(void);

#endif
