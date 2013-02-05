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
    int fd;
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
    eventProc *readProc;
    eventProc *writeProc;
};

struct evcenter *eventcenter_init(int nevent, eventProc *read, eventProc *write);
void eventcenter_dealloc(struct evcenter *center);
int createEvent(struct evcenter *center, int fd, int mask, void *clientData);
void deleteEvent(struct evcenter *center, int fd, int mask);

#endif
