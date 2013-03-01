#include "wheatserver.h"
#include "protocol.h"

struct protocol protocolTable[] = {
    {"Http", parseHttp, initHttpData, freeHttpData, initHttp, deallocHttp}
};

struct protocol *spotProtocol(char *ip, int port, int fd)
{
    static int is_init = 0;
    if (!is_init) {
        protocolTable[0].initProtocol();
        is_init = 1;
    }
    return &protocolTable[0];
}
