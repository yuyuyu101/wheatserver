#include "../wheatserver.h"
#include "protocol.h"

struct protocol *spotProtocol(char *ip, int port, int fd)
{
    static int is_init = 0;
    if (!is_init) {
        int ret = ProtocolTable[0].initProtocol();
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_WARNING, "init protocol failed");
            return NULL;
        }
        is_init = 1;
    }
    return &ProtocolTable[0];
}
