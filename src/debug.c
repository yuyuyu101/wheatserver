// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "wheatserver.h"
#include <execinfo.h>

void wheat_stacktrace(int skip_count)
{
    void *stack[64];
    char **symbols;
    int size, i, j;

    size = backtrace(stack, 64);
    symbols = backtrace_symbols(stack, size);
    if (symbols == NULL) {
        return;
    }

    skip_count++; /* skip the current frame also */

    for (i = skip_count, j = 0; i < size; i++, j++) {
        wheatLog(WHEAT_WARNING, "[%d] %s", j, symbols[i]);
    }

    wfree(symbols);
}

void wheat_assert(const char *cond, const char *file, int line, int panic)
{
    wheatLog(WHEAT_WARNING, "assert '%s' failed @ (%s, %d)", cond, file, line);
    if (panic) {
        wheat_stacktrace(1);
        abort();
    }
}
