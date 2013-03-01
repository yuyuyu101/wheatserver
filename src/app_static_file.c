#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/types.h>

#include "wheatserver.h"
#include "proto_http.h"

static unsigned int max_filesize = WHEAT_MAX_BUFFER_SIZE;

int staticFileCall(struct client *c, void *arg)
{
    wstr path = arg;
    int file_d = open(path, O_RDONLY), ret;
    off_t len, send;
    if (file_d == -1) {
        goto failed;
    }
    ret = getFileSize(file_d, &len);
    if (ret == WHEAT_WRONG || len > max_filesize) {
        goto failed;
    }
    send = len;

    ret = httpSendHeaders(c);
    if (ret == -1)
        goto failed;
    ret = sendfile(file_d, c->clifd, 0, &send, NULL, 0);
    while (send < len) {
        int nread;
        lseek(file_d, send, SEEK_SET);
        if (ret == -1) {
            goto failed;
        }
        nread = readBulkFrom(file_d, &c->res_buf);
        if (nread <= 0) {
            goto failed;
        }
        send += nread;
    }
    ret = WorkerProcess->worker->sendData(c);
    if (ret == -1)
        goto failed;
    return WHEAT_OK;

failed:
    sendResponse404(c);
    return WHEAT_WRONG;
}

void initStaticFile()
{
    struct configuration *conf = getConfiguration("file-maxsize");
    max_filesize = conf->target.val;
}

void deallocStaticFile()
{
}

void *initStaticFileData()
{
    return NULL;
}

void freeStaticFileData(void *app_data)
{

}
