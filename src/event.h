#ifndef _EVENT_H_
#define _EVENT_H_

#define EVENT_NONE 0
#define EVENT_READABLE 1
#define EVENT_WRITABLE 2

struct evcenter;

typedef void eventProc(struct evcenter *center, int fd, void *clientData, int mask);

struct event {
    int mask;
    void *client_data;
    eventProc *readProc;
    eventProc *writeProc;
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

struct evcenter *eventcenter_init(int nevent);
void eventcenter_dealloc(struct evcenter *center);
int createEvent(struct evcenter *center, int fd, int mask, eventProc *proc, void *clientData);
void deleteEvent(struct evcenter *center, int fd, int mask);
int processEvents(struct evcenter *center, int timeout_seconds);

#endif
