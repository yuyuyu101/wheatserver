#ifndef WHEATSERVER_LIST_H
#define WHEATSERVER_LIST_H

#include <stdint.h>

/* `value` is pointer, generally, list not own it only store
 * the pointer value. But if dup and free exist, list own it. */
#define isListOwnValue(list) ((list)->dup && (list)->free)

struct listNode {
    struct listNode *next;
    struct listNode *prev;
    intptr_t value;
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

struct list *createList();
void freeList(struct list *);
void listClean(struct list *l);
struct listNode *appendToListTail(struct list *, void *ptr);
struct listNode *insertToListHead(struct list *, void *ptr);
void removeListNode(struct list *l, struct listNode *node);
struct listNode *searchListKey(struct list *l, void *key);
struct listIterator *listGetIterator(struct list *list, int direction);
void listRotate(struct list *l);
struct listNode *listNext(struct listIterator *iter);
void freeListIterator(struct listIterator *iter);
void listPrint(struct list *list);

#endif
