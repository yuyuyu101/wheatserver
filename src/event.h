// A simple event-driven library learn from Redis. It support multi
// event-notify API and easy to extend more platforms.
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_EVENT_H_
#define WHEATSERVER_EVENT_H_

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

// We use epoll, kqueue, evport, select in descending order by performance.
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#ifdef __sun
#include <sys/feature_tests.h>
#ifdef _DTRACE_VERSION
#define HAVE_EVPORT 1
#endif
#endif

#define EVENT_NONE 0
#define EVENT_READABLE 1
#define EVENT_WRITABLE 2

struct evcenter;

// Attention:
// This event library use file description as index to search correspond event
// in `events` and `fired_events`. So it's important to estimate a suitable
// capacity in calling eventcenterInit(capacity).

typedef void eventProc(struct evcenter *center, int fd, void *client_data,
        int mask);

struct event {
    int mask;
    void *client_data;
    eventProc *read_proc;
    eventProc *write_proc;
};

struct fired_event {
    int mask;
    int fd;
};

struct evcenter {
    int nevent;
    struct event *events;
    struct fired_event *fired_events;
    void *apidata;
};

struct evcenter *eventcenterInit(int nevent);
void eventcenterDealloc(struct evcenter *center);

int createEvent(struct evcenter *center, int fd, int mask, eventProc *proc,
        void *client_data);
void deleteEvent(struct evcenter *center, int fd, int mask);
int processEvents(struct evcenter *center, int timeout_milliseconds);

#endif
