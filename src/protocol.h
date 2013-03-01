#ifndef WHEATSERVER_PROTOCOL_H
#define WHEATSERVER_PROTOCOL_H

int parseHttp(struct client *);
void *initHttpData();
void freeHttpData(void *data);
void initHttp();
void deallocHttp();

#endif
