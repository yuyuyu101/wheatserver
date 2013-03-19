#include "../wheatserver.h"
#include "protocol.h"

struct protocol *spotProtocol(char *ip, int port, int fd)
{
    static int is_init = -1;
    if (is_init == -1) {
        int i = 0;
        struct configuration *conf = getConfiguration("protocol");
        for (; i < 2; ++i) {
            struct protocol *p = &ProtocolTable[i];
            if (!strcasecmp(conf->target.ptr, p->name))
                is_init = i;
        }
        if (is_init == -1) {
            wheatLog(WHEAT_WARNING, "spot protocol failed");
            return NULL;
        }
        int ret = ProtocolTable[is_init].initProtocol();
        if (ret == WHEAT_WRONG) {
            wheatLog(WHEAT_WARNING, "init protocol failed");
            return NULL;
        }
    }
    return &ProtocolTable[is_init];
}
