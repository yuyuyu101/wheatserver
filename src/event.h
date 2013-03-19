#ifndef WHEATSERVER_EVENT_H_
#define WHEATSERVER_EVENT_H_

/* Test for polling API */
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

typedef void eventProc(struct evcenter *center, int fd, void *clientData, int mask);

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
int createEvent(struct evcenter *center, int fd, int mask, eventProc *proc, void *client_data);
void deleteEvent(struct evcenter *center, int fd, int mask);
int processEvents(struct evcenter *center, int timeout_seconds);

#endif
