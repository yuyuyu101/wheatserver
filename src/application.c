#include "wheatserver.h"
#include "application.h"

struct app appTable[] = {
    {"wsgi", wsgiConstructor, initWsgi, deallocWsgi, initWsgiAppData, freeWsgiAppData}
};

struct app *spotAppInterface()
{
    static int is_init = 0;
    if (!is_init) {
        appTable[0].initApp();
        is_init = 1;
    }
    return &appTable[0];
}
