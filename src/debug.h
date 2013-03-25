// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_DEBUG_H
#define WHEATSERVER_DEBUG_H

void wheat_stacktrace(int skip_count);
void wheat_assert(const char *cond, const char *file, int line, int panic);

#define ASSERT(_x) do {                            \
    if (!(_x)) {                                   \
        wheat_assert(#_x, __FILE__, __LINE__, 1);  \
    }                                              \
} while (0)

#endif
