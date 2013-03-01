#ifndef WHEATSERVER_PROTOCOL_H
#define WHEATSERVER_PROTOCOL_H

int httpSpot(struct client*);
int parseHttp(struct client *);
void *initHttpData();
void freeHttpData(void *data);
void initHttp();
void deallocHttp();

#endif
