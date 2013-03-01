#include "wheatserver.h"
#include "application.h"

struct app appTable[] = {
    {"http", "wsgi", wsgiCall, initWsgi, deallocWsgi,
        initWsgiAppData, freeWsgiAppData, 0},
    {"http", "static-file", staticFileCall, initStaticFile, deallocStaticFile,
        initStaticFileData, freeStaticFileData, 0}
};
