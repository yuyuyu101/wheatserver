#include "worker_process.h"

/* Sync worker Area */
void setupSync();
void syncWorkerCron();
int syncSendData(struct client *c, wstr data); // pass `data` ownership to
int syncRecvData(struct client *c);

/* Async worker Area */
void setupAsync();
void asyncWorkerCron();
int asyncSendData(struct client *c, wstr data); // pass `data` ownership to
int asyncRecvData(struct client *c);

struct worker WorkerTable[] = {
    {"SyncWorker", setupSync, syncWorkerCron, syncSendData, syncRecvData},
    {"AsyncWorker", setupAsync, asyncWorkerCron, asyncSendData, asyncRecvData}
};

int httpSpot(struct client*);
int parseHttp(struct client *);
void *initHttpData();
void freeHttpData(void *data);
void initHttp();
void deallocHttp();

struct protocol ProtocolTable[] = {
    {"Http", httpSpot, parseHttp, initHttpData, freeHttpData,
        initHttp, deallocHttp}
};

int staticFileCall(struct client *, void *);
void initStaticFile();
void deallocStaticFile();
void *initStaticFileData(struct client *);
void freeStaticFileData(void *app_data);

int wsgiCall(struct client *, void *);
void initWsgi();
void deallocWsgi();
void *initWsgiAppData(struct client *);
void freeWsgiAppData(void *app_data);

struct app appTable[] = {
    {"http", "wsgi", wsgiCall, initWsgi, deallocWsgi,
        initWsgiAppData, freeWsgiAppData, 0},
    {"http", "static-file", staticFileCall, initStaticFile, deallocStaticFile,
        initStaticFileData, freeStaticFileData, 0}
};
