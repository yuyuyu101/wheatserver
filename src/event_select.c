// Select()-based event.c module.
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
/*
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <string.h>
#include <sys/select.h>
#include <stdlib.h>

#include "memalloc.h"
#include "event.h"

struct apiState {
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    fd_set _rfds, _wfds;
    int max_fd;
};

static struct apiState *eventInit(int nevent)
{
    struct apiState *state = wmalloc(sizeof(struct apiState));

    if (!state) return NULL;
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    state->max_fd = 0;
    return state;
}

static void eventDeinit(struct apiState *data)
{
    wfree(data);
}

static int addEvent(struct evcenter *center, int fd, int mask)
{
    struct apiState *state = center->apidata;
    if (mask & EVENT_READABLE) FD_SET(fd, &state->rfds);
    if (mask & EVENT_WRITABLE) FD_SET(fd, &state->wfds);
    if (fd > state->max_fd)
        state->max_fd = fd;
    return 0;
}

static void delEvent(struct evcenter *center, int fd, int mask)
{
    struct apiState *state = center->apidata;
    if (mask & EVENT_READABLE) FD_CLR(fd, &state->rfds);
    if (mask & EVENT_WRITABLE) FD_CLR(fd, &state->wfds);
}

static int eventWait(struct evcenter *center, struct timeval *tvp) {
    struct apiState *state = center->apidata;
    int retval, j, numevents = 0;

    memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
    memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

    retval = select(state->max_fd+1, &state->_rfds, &state->_wfds, NULL, tvp);
    if (retval > 0) {
        for (j = 0; j <= state->max_fd; j++) {
            int mask = 0;
            struct event *fe = &center->events[j];

            if (fe->mask == EVENT_NONE) continue;
            if (fe->mask & EVENT_READABLE && FD_ISSET(j, &state->_rfds))
                mask |= EVENT_READABLE;
            if (fe->mask & EVENT_WRITABLE && FD_ISSET(j, &state->_wfds))
                mask |= EVENT_WRITABLE;
            center->fired_events[numevents].fd = j;
            center->fired_events[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}
