// Hash table implemetation
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_DICT_H
#define WHEATSERVER_DICT_H

#define DICT_OK 0
#define DICT_WRONG 1

#define DICT_HT_INITIAL_SIZE 16
#define DICT_FORCE_RESIZE_RATIO 5

#include <stdint.h>

// Key, value ownership:
// If `keyDup` in `type` field in struct dict is specified, dict will duplicate
// the key. If `keyDup` is empty, caller must guarantee the live of key.
// Like `keyDup`, `valDup` is the same.
//
// In general, you could create dictType like wstrDictType: don't set `keyDup`
// and `valDup`, but set `keyDestructor` and `valDestructor`. In this way, you
// can pass ownership to dict and needn't to free it. And it reduce unnecessary
// allocation and release operates.
//
// Performance:
// You should be cautious of insert much key and value pairs to dict. If you
// are sensitive to performance, try to call dictExpand prepared. It will
// expand slots in dict.
//
// Traversing hash table example:
//     struct dict *d = dictCreate(&wstrDictType);
//     ...
//     dictAdd(key, value);
//     dictAdd(key, value);
//     ...
//     struct dictIterator *iter = dictGetIterator(d);
//     struct dictEntry *entry;
//     while ((entry = dictNext(iter)) != NULL) {
//         key = dictGetKey(entry);
//         value = dictGetVal(entry);
//         ...
//     }
//     dictReleaseIterator(iter);

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

// This is our hash table structure.
// `size`: the slots for hash
// `sizemask`: the upper bound for `size`, using for getting slot within `size`
// `used`: the number of entries.
// `type`: the methods for operate key and value
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

// Base Dict API
struct dict *dictCreate(struct dictType*);
int dictAdd(struct dict *d, void *key, void *val);
int dictReplace(struct dict *d, void *key, void *val, int *replace);
int dictDelete(struct dict *d, const void *key);
// dictDeleteNoFree is used if you don't want to free key and value when
// `keyDestructor` and `valDestructor` is set.
int dictDeleteNoFree(struct dict *d, const void *key);
struct dictEntry * dictFind(struct dict *d, const void *key);
void *dictFetchValue(struct dict *d, const void *key);
void dictRelease(struct dict *d);

// Iterator operators
struct dictIterator *dictGetIterator(struct dict *d);
struct dictEntry *dictNext(struct dictIterator *iter);
void dictReleaseIterator(struct dictIterator *iter);

// Low level API
int dictExpand(struct dict *d, unsigned long size);
struct dictEntry *dictAddRaw(struct dict *d, void *key);
struct dictEntry *dictReplaceRaw(struct dict *d, void *key);
void dictClear(struct dict *d);

// Debug use, it may crash affecting the whole application
void dictPrintStats(struct dict *d);
void dictPrint(struct dict *d);

// Hash function, using them for customize key, value pair
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
unsigned int dictGetHashFunctionSeed(void);
void dictSetHashFunctionSeed(unsigned int initval);

#endif
