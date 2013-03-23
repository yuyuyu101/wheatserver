#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "array.h"
#include "memalloc.h"

struct array {
    size_t nelem;
    uint8_t *elements;
    size_t elem_size;
    size_t capacity;
};

struct array *arrayCreate(size_t elem_size, size_t cap)
{
    size_t data_len = elem_size * cap;
    struct array *arr = wmalloc(sizeof(struct array));
    arr->nelem = 0;
    arr->elem_size = elem_size;
    arr->elements = wmalloc(data_len);
    arr->capacity = cap;
    return arr;
}

void arrayDealloc(struct array *arr)
{
    wfree(arr->elements);
    wfree(arr);
}

void *arrayIndex(struct array *a, size_t pos)
{
    if (pos >= a->nelem)
        return NULL;
    size_t p = pos * a->elem_size;
    return a->elements+p;
}

void arraySet(struct array *a, void *data, size_t p)
{
    if (p > a->nelem)
        return ;
    size_t pos = p * a->elem_size;
    if (a->nelem == a->capacity) {
        size_t new_cap = a->capacity * 2;
        uint8_t *p = wrealloc(a->elements, a->elem_size*new_cap);
        a->elements = p;
        a->capacity = new_cap;
    }
    memcpy(a->elements+pos, data, a->elem_size);
    if (p == a->nelem)
        a->nelem++;
}

void arrayPush(struct array *a, void *data)
{
    arraySet(a, data, a->nelem);
}

void *arrayPop(struct array *a)
{
    return a->elements+a->elem_size*(--a->nelem);
}

void *arrayTop(struct array *a)
{
    return arrayIndex(a, 0);
}

void arrayEach(struct array *a, void(*func)(void *))
{
    size_t len = a->nelem;
    uint8_t *pos = a->elements;
    while (len--) {
        func(pos);
        pos += a->elem_size;
    }
}

void arrayEach2(struct array *a, void(*func)(void *item, void *data), void *data)
{
    size_t len = a->nelem;
    uint8_t *pos = a->elements;
    while (len--) {
        func(pos, data);
        pos += a->elem_size;
    }
}

size_t narray(struct array *a)
{
    return a->nelem;
}

#ifdef ARRAY_TEST_MAIN
#include "test_help.h"
#include "stdlib.h"
#include "stdio.h"

int main(int argc, const char *argv[])
{
    {
        struct array *a = arrayCreate(sizeof(int), 10);
        int i;
        for (i = 0; i < 11; ++i) {
            arrayPush(a, &i);
        }
        test_cond("arrayTop", *(int *)(arrayTop(a)) == 0);
        for (i = 0; i < 10; ++i) {
            int *j = arrayPop(a);
            test_cond("arrayPop", *j == 10-i);
        }
        i = 10;
        arrayPush(a, &i);
        i = 9;
        arrayPush(a, &i);
        test_cond("arrayTop", *(int *)(arrayTop(a)) == 0);
        arrayDealloc(a);
    }
    test_report();
    return 0;
}

#endif
