// Implementation of double linked-list
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_LIST_H
#define WHEATSERVER_LIST_H

#include <stdint.h>

// Ownership:
// List duplicate value when `dup` set.
// List free value when `free` set.
// List compare value when `match` set.
//
// User guide:
// Don't recommand you to set `dup` method, and if you want to reduce jobs on
// elements free, you could set free method and for ease.
//
// Traversing example:
//     struct list *l = createList();
//     appendToListTail(l);
//     appendToListTail(l);
//     appendToListTail(l);
//     ...
//     struct listIterator *iter = listGetIterator(l, START_HEAD);
//     struct listNode *node;
//     while ((node = listNext(iter)) != NULL) {
//         struct data *data = listNodeValue(node);
//         ...
//     }
//     freeListIterator(iter);
//
//  Delete guide:
//  Because list is implemented by double link, and it's convient to remove
//  node from linked list. You could save the node for deleted and call:
//      removeListNode(l, node);

struct listNode {
    struct listNode *next;
    struct listNode *prev;
    void *value;
};

struct list {
    struct listNode *first, *last;
    void *(*dup)(void *ptr);
    int (*match)(void *ptr, void *key);
    void (*free)(void *ptr);
    unsigned long len;
};

#define listLength(list) ((list)->len)
#define listFirst(list) ((list)->first)
#define listLast(list) ((list)->last)
#define listNodeValue(list) ((void*)((list)->value))

#define listSetDup(list, m) ((list)->dup = (m))
#define listSetFree(list, m) ((list)->free = (m))
#define listSetMatch(list, m) ((list)->match = (m))

struct listIterator {
    struct listNode *next;
    int direction;
};

#define START_HEAD 0
#define START_TAIL 1

// Base list API
struct list *createList();
void freeList(struct list *);
struct listNode *appendToListTail(struct list *, void *ptr);
struct listNode *insertToListHead(struct list *, void *ptr);
void removeListNode(struct list *l, struct listNode *node);
struct listNode *searchListKey(struct list *l, void *key);
void listClear(struct list *l);

// Traversing list
struct listIterator *listGetIterator(struct list *list, int direction);
void listRotate(struct list *l);
struct listNode *listNext(struct listIterator *iter);
void freeListIterator(struct listIterator *iter);
void listEach(struct list *l, void(*func)(void *));
void listEach2(struct list *l, void(*func)(void *, void*), void *data);

// Debug use
void listPrint(struct list *list);

#endif
