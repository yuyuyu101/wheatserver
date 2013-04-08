// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_ARRAY_H
#define WHEATSERVER_ARRAY_H

struct array;

// `array` is a structure like traditional linear table. The elements in array
// is ordered and continuously.
//
// | element 1 | element 2| element 3 | ... | element N|
//
// If you want iterate the whole array, there are two ways for you and try to
// use one of them:
//
// 1. Iterate directly
//     struct array *array = arrayCreate(...);
//     ...
//     arrayPush(...);
//     arrayPush(...);
//     arrayPush(...);
//     ...
//     struct data *data, *data_array = arrayData(array);
//     size_t len = narray(array);
//     for (i = 0; i < len; ++i) {
//         data = data_array[i];
//         ...
//     }
//
// 2. Iterate by function
//     static void operateOnArrayItem(void *item)
//     {
//         struct data *data = item;
//         ...
//     }
//     static void operateOnArrayItem2(void *item, void *client_data)
//     {
//         struct data *data = item;
//         ...
//     }
//     arrayEach(array, operateOnArrayItem);
//  OR
//     arrayEach2(array, operateOnArrayItem2, client_data);
//
// You can expand array transparently.  If you are sensitive to performance on
// memory moving(expanding array may cause memory move), you should create a
// enough big on argument for `capacity`.

// Modified methods
struct array *arrayCreate(size_t elem_size, size_t cap);
struct array *arrayDup(struct array *a);
void arrayDealloc(struct array *arr);
void arraySet(struct array *a, void *data, size_t p);
void arrayPush(struct array *a, void *data);
void *arrayPop(struct array *a);

// Unmodified methods
void *arrayIndex(struct array *a, size_t pos);
void *arrayTop(struct array *a);
void *arrayLast(struct array *a);
void arrayEach(struct array *a, void(*func)(void *));
void arrayEach2(struct array *a, void(*func)(void *, void *), void *data);
void *arrayData(struct array *a);
size_t narray(struct array *a);

#endif
