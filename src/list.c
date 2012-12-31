#include <stdlib.h>

#include "list.h"

struct list *createList()
{
    struct list *l = malloc(sizeof(struct list));
    l->first = l->last = NULL;
    l->len = 0;
    l->dup = NULL;
    l->match = NULL;
    l->free = NULL;
    return l;
}

void freeList(struct list *l)
{
    struct listNode *current, *next;
    unsigned long len = l->len;

    current = l->first;
    while (len--) {
        next = current->next;
        if (isListOwnValue(l))
            l->free(current->value);
        free(current);
        current = next;
    }
    free(l);
}

struct list *appendToListTail(struct list *l, void *ptr)
{
    struct listNode *newNode = malloc(sizeof(struct listNode));
    if (newNode == NULL)
        return NULL;
    if (isListOwnValue(l))
        newNode->value = l->dup(ptr);
    else
        newNode->value = ptr;
    newNode->prev = l->last;
    newNode->next = NULL;

    if (l->len == 0)
        l->first = newNode;
    else
        l->last->next = newNode;
    l->last = newNode;
    l->len++;
    return l;
}

struct list *insertToListHead(struct list *l, void *ptr)
{
    struct listNode *newNode = malloc(sizeof(struct listNode));
    if (newNode == NULL)
        return NULL;
    if (isListOwnValue(l))
        newNode->value = l->dup(ptr);
    else
        newNode->value = ptr;
    newNode->prev = NULL;
    newNode->next = l->first;

    if (l->len == 0)
        l->last = newNode;
    else
        l->first->prev = newNode;
    l->first = newNode;
    l->len++;
    return l;
}

void removeListNode(struct list *l, struct listNode *node)
{
    if (node == NULL)
        return ;

    if (isListOwnValue(l))
        l->free(node->value);
    if (node == l->first) {
        l->first = node->next;
        l->first->prev = NULL;
    } else if (node == l->last) {
        l->last = node->prev;
        l->last->next = NULL;
    } else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    l->len--;
    if (l->len == 0)
        l->first = l->last = NULL;
    free(node);
}

struct listNode *searchListKey(struct list *l, void *key)
{
    struct listIterator *iter = listGetIterator(l, START_HEAD);
    struct listNode *current;

    while ((current = listNext(iter)) != NULL) {
        if (l->match) {
            if (l->match(current->value, key)) {
                freeListIterator(iter);
                return current;
            }
        } else {
            if (current->value == key) {
                freeListIterator(iter);
                return current;
            }
        }
    }
    freeListIterator(iter);
    return NULL;
}

struct listIterator *listGetIterator(struct list *list, int direction)
{
    struct listIterator *iter = malloc(sizeof(struct listIterator));
    if (iter == NULL)
        return NULL;
    if (direction == START_HEAD) {
        iter->next = list->first;
        iter->direction = START_HEAD;
    }
    else {
        iter->next = list->last;
        iter->direction = START_TAIL;
    }
    return iter;
}

void freeListIterator(struct listIterator *iter)
{
    free(iter);
}

struct listNode *listNext(struct listIterator *iter)
{
    struct listNode *current = iter->next;
    if (current == NULL)
        return NULL;
    if (iter->direction == START_HEAD)
        iter->next = current->next;
    else if (iter->direction == START_TAIL)
        iter->next = current->prev;
    return current;
}

#ifdef LIST_TEST_MAIN
#include "test_help.h"
#include <stdio.h>

void *dupInt(void *ptr)
{
    int *new_ptr = malloc(sizeof(int));
    *new_ptr = *(int *)ptr;
    return new_ptr;
}

void freeInt(void *ptr)
{
    free(ptr);
}

int matchInt(void *ptr, void *key)
{
    return (*(int *)ptr == *(int *)key);
}

int main(void)
{
    {
        struct list *l;
        l = createList();
        test_cond("Create a list and obtain the length",
                listLength(l) == 0 && l != NULL);
        freeList(l);

        l = createList();
        int i;
        for (i = 0; i < 10; i++) {
            int *n = malloc(sizeof(int));
            *n = i;
            appendToListTail(l, n);
        }
        test_cond("Append to list tail length", listLength(l) == 10);
        struct listIterator *iter = listGetIterator(l, 0);
        for (i = 0; i < 10; i++) {
            int *n = listNodeValue(listNext(iter));
            test_cond("append to list equal",
                    *n == i);
        }
        freeList(l);
        freeListIterator(iter);

        l = createList();
        listSetDup(l, dupInt);
        listSetFree(l, freeInt);
        listSetMatch(l, matchInt);
        test_cond("list own value", isListOwnValue(l));
        for (i = 0; i < 10; i++) {
            int *n = malloc(sizeof(int));
            *n = i;
            insertToListHead(l, n);
            free(n);
        }
        test_cond("Append to list tail length", listLength(l) == 10);
        iter = listGetIterator(l, 1);
        for (i = 0; i < 10; i++) {
            int *n = listNodeValue(listNext(iter));
            test_cond("append to list equal", *n == i);
        }
        freeListIterator(iter);
        int d = 1;
        insertToListHead(l, &d);
        d = 2;
        int *store = listNodeValue(listFirst(l));
        test_cond("list own value test", listLength(l) == 11 &&
                 *store == 1);
        removeListNode(l, listFirst(l));
        test_cond("list own value remove test", listLength(l) == 10);
        struct listNode *s = searchListKey(l, &d);
        test_cond("searchListKey matched", *((int *)(s->value)) == 2);
        removeListNode(l, s);
        s = searchListKey(l, &d);
        test_cond("searchListKey not matched", s == NULL && listLength(l) == 9);
        freeList(l);
    }
    test_report();
}
#endif
