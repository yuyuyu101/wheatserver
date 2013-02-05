#include <sys/event.h>
#include <unistd.h>
#include <stdlib.h>

#include "event.h"

struct apiState {
    int kqfd;
    struct kevent *events;
};

static struct apiState *eventInit(int nevent)
{
    struct kevent *event = NULL;
    int ep;
    struct apiState *state = NULL;

    ep = kqueue();
    if (ep < 0) {
        return NULL;
    }

    state = malloc(sizeof(struct apiState));
    if (!state) {
        close(ep);
        return NULL;
    }

    event = malloc(nevent*sizeof(struct kevent));
    if (event == NULL) {
        close(ep);
        free(state);
        return NULL;
    }
    return state;
}

static void eventDeinit(struct apiState *data)
{
    close(data->kqfd);
    free(data->events);
    free(data);
}

static int addEvent(struct apiState *state, int fd, int mask) {
    struct kevent ke;

    if (mask & EVENT_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    if (mask & EVENT_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    return 0;
}

static void delEvent(struct apiState *state, int fd, int mask) {
    struct kevent ke;

    if (mask & EVENT_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }

    if (mask & EVENT_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
}


static int eventWait(struct evcenter *center, struct timeval *tvp) {
    struct apiState *state = center->apidata;
    int retval, numevents = 0;

    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        retval = kevent(state->kqfd, NULL, 0, state->events, center->nevent,
                        &timeout);
    } else {
        retval = kevent(state->kqfd, NULL, 0, state->events, center->nevent,
                        NULL);
    }

    if (retval > 0) {
        int j;

        numevents = retval;
        for(j = 0; j < numevents; j++) {
            int mask = 0;
            struct kevent *e = state->events + j;

            if (e->filter == EVFILT_READ) mask |= EVENT_READABLE;
            if (e->filter == EVFILT_WRITE) mask |= EVENT_WRITABLE;
            center->events[j].fd = e->ident;
            center->events[j].mask = mask;
        }
    }
    return numevents;
}
