// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "wheatserver.h"

#ifdef HAVE_EPOLL
#include "event_epoll.c"
#else
#ifdef HAVE_KQUEUE
#include "event_kqueue.c"
#else
#include "event_select.c"
#endif
#endif

struct evcenter *eventcenterInit(int nevent)
{
    struct event *events = NULL;
    struct evcenter *center = NULL;
    struct fired_event *fired_events = NULL;
    void *api_state = NULL;

    center = wmalloc(sizeof(struct evcenter));
    if (center == NULL) {
        wheatLog(WHEAT_WARNING, "center create failed: %s", strerror(errno));
        return NULL;
    }
    events = wmalloc(nevent*sizeof(struct event));
    fired_events = wmalloc(nevent*sizeof(struct fired_event));
    if (fired_events == NULL || events == NULL) {
        wheatLog(WHEAT_WARNING, "event and fired_event create failed: %s",
                strerror(errno));
        goto cleanup;
    }
    memset(events, 0, nevent*sizeof(struct event));
    memset(fired_events, 0, nevent*sizeof(struct fired_event));

    api_state = eventInit(nevent);
    if (!api_state) {
        wheatLog(WHEAT_WARNING, "naive event init failed", strerror(errno));
        goto cleanup;
    }

    center->nevent = nevent;
    center->events = events;
    center->fired_events = fired_events;
    center->apidata = api_state;

    return center;

cleanup:
    if (center)
        wfree(center);
    if (fired_events)
        wfree(fired_events);
    if (events)
        wfree(events);
    return NULL;
}

void eventcenterDealloc(struct evcenter *center)
{
    eventDeinit(center->apidata);
    wfree(center->events);
    wfree(center);
}

int createEvent(struct evcenter *center, int fd, int mask, eventProc *proc, void *client_data)
{
    if (fd >= center->nevent) {
        errno = ERANGE;
        return WHEAT_WRONG;
    }
    struct event *event = &center->events[fd];

    if (addEvent(center, fd, mask) == -1)
        return WHEAT_WRONG;
    event->mask |= mask;
    if (mask & EVENT_READABLE) event->read_proc = proc;
    if (mask & EVENT_WRITABLE) event->write_proc = proc;
    event->client_data = client_data;
    return WHEAT_OK;
}

void deleteEvent(struct evcenter *center, int fd, int mask)
{
    if (fd >= center->nevent) return;
    struct event *event = &center->events[fd];

    if (event->mask == EVENT_NONE) return;
    event->mask = event->mask & (~mask);
    delEvent(center, fd, mask);
}

int processEvents(struct evcenter *center, int timeout_seconds)
{
    struct timeval tv;
    tv.tv_usec = 0;
    if (timeout_seconds > 0)
        tv.tv_sec = timeout_seconds;
    else
        tv.tv_sec = 0;
    int j, processed = 0, numevents = eventWait(center, &tv);
    for (j = 0; j < numevents; j++) {
        struct event *event = &center->events[center->fired_events[j].fd];
        int mask = center->fired_events[j].mask;
        int fd = center->fired_events[j].fd;
        int rfired = 0;

        /* note the fe->mask & mask & ... code: maybe an already processed
         * event removed an element that fired and we still didn't
         * processed, so we check if the event is still valid. */
        if (event->mask & mask & EVENT_READABLE) {
            rfired = 1;
            event->read_proc(center, fd, event->client_data, mask);
        }
        if (event->mask & mask & EVENT_WRITABLE) {
            if (!rfired || event->read_proc != event->write_proc)
                event->write_proc(center, fd, event->client_data, mask);
        }
        processed++;
    }
    return processed;
}
