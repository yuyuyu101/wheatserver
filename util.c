#include "wheatserver.h"

void nonBlockCloseOnExecPipe(int *fd0, int *fd1)
{
    int full_pipe[2];
    if (pipe(full_pipe) != 0) {
        wheatLog(WHEAT_WARNING, "Create pipe failed: %s", strerror(errno));
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[0]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[0], Server.neterr);
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[1]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[1], Server.neterr);
        halt(1);
    }
    *fd0 = full_pipe[0];
    *fd1 = full_pipe[1];
}


unsigned int dictWstrHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, wstrlen((char*)key));
}

unsigned int dictWstrCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, wstrlen((char*)key));
}

int dictWstrKeyCompare(const void *key1,
        const void *key2)
{
    int l1,l2;

    l1 = wstrlen((wstr)key1);
    l2 = wstrlen((wstr)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictWstrDestructor(void *val)
{
    wstrFree(val);
}

struct dictType wstrDictType = {
    dictWstrHash,               /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictWstrKeyCompare,         /* key compare */
    dictWstrDestructor,         /* key destructor */
    dictWstrDestructor,         /* val destructor */
};
