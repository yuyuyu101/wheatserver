#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>

#include "event.h"
#include "memalloc.h"

struct apiState {
    int epfd;
    struct epoll_event *events;
};

static struct apiState *eventInit(int nevent) {
    struct apiState *state = wmalloc(sizeof(struct apiState));

    if (!state) return NULL;
    state->events = wmalloc(sizeof(struct epoll_event)*nevent);
    if (!state->events) {
        wfree(state);
        return NULL;
    }
    state->epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    if (state->epfd == -1) {
        wfree(state->events);
        wfree(state);
        return NULL;
    }
    return state;
}

static void eventDeinit(struct apiState *state) {
    close(state->epfd);
    wfree(state->events);
    wfree(state);
}

static int addEvent(struct evcenter *center, int fd, int mask) {
    struct apiState *state = center->apidata;
    struct epoll_event ee;
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    int op = center->events[fd].mask == EVENT_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= center->events[fd].mask; /* Merge old events */
    if (mask & EVENT_READABLE) ee.events |= EPOLLIN;
    if (mask & EVENT_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

static void delEvent(struct evcenter *center, int fd, int delmask) {
    struct apiState *state = center->apidata;
    struct epoll_event ee;
    int mask = center->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & EVENT_READABLE) ee.events |= EPOLLIN;
    if (mask & EVENT_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (mask != EVENT_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int eventWait(struct evcenter *center, struct timeval *tvp) {
    struct apiState *state = center->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, center->nevent,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events + j;

            if (e->events & EPOLLIN) mask |= EVENT_READABLE;
            if (e->events & EPOLLOUT) mask |= EVENT_WRITABLE;
            if (e->events & EPOLLERR) mask |= EVENT_WRITABLE;
            if (e->events & EPOLLHUP) mask |= EVENT_WRITABLE;
            center->fired_events[j].fd = e->data.fd;
            center->fired_events[j].mask = mask;
        }
    }
    return numevents;
}
