#include "wheatserver.h"


/* because readBulkFrom call wstrMakeRoom realloc memory pointer,
 * pass a pointer point pointer is needed.
 * return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN */
int readBulkFrom(int fd, wstr *clientbuf)
{
    ssize_t nread, readlen;
    int qblen;
    wstr buf = *clientbuf;

    readlen = WHEAT_IOBUF_LEN;

    qblen = wstrlen(buf);
    buf = wstrMakeRoom(buf, readlen);

    if (wstrlen(buf) > Server.max_buffer_size) {
        wheatLog(WHEAT_NOTICE, "buffer size larger than max limit");
        return WHEAT_WRONG;
    }

    // Important ! buf may changed
    *clientbuf = buf;

    nread = read(fd, buf+qblen, readlen);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            wheatLog(WHEAT_VERBOSE, "Reading from fd %d: %s", fd, strerror(errno));
            return WHEAT_WRONG;
        }
    } else if (nread == 0) {
        wheatLog(WHEAT_DEBUG, "Peer close file descriptor %d", fd);
        return WHEAT_WRONG;
    }
    if (nread) {
        wstrupdatelen(buf, (int)(qblen+nread));
    }
    return (int)nread;
}

/* return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN */
int writeBulkTo(int fd, wstr *clientbuf)
{
    wstr buf = *clientbuf;
    ssize_t nwritten = 0;
    nwritten = write(fd, buf, wstrlen(buf));
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else if (errno == EPIPE) {
            wheatLog(WHEAT_DEBUG, "Receive RST, peer closed", strerror(errno));
            return WHEAT_WRONG;
        } else {
            wheatLog(WHEAT_NOTICE,
                "Error writing to client: %s", strerror(errno));
            return WHEAT_WRONG;
        }
    }
    wstrRange(buf, (int)nwritten, 0);
    return (int)nwritten;
}

static void sendReplyToClient(struct evcenter *center, int fd, void *data, int mask)
{
    struct masterClient *client = data;
    size_t bufpos = 0, totallen = wstrlen(client->response_buf);
    ssize_t nwritten;

    while (bufpos < totallen) {
        nwritten = writeBulkTo(client->fd, &client->response_buf);
        if (nwritten <= 0)
            break;
        bufpos += nwritten;
    }
    if (nwritten == -1) {
        freeMasterClient(client);
        return ;
    }
    if (bufpos == totallen) {
        deleteEvent(center, fd, EVENT_WRITABLE);
    }
}

ssize_t isClientPreparedWrite(int fd, struct evcenter *center, void *c)
{
    if (fd <= 0 || createEvent(center, fd, EVENT_WRITABLE, sendReplyToClient, c) == WHEAT_WRONG)
        return WHEAT_WRONG;
    return WHEAT_OK;
}

void replyMasterClient(struct masterClient *c, const char *buf, size_t len)
{
    if (isClientPreparedWrite(c->fd, Server.master_center, c) == WHEAT_WRONG)
        return ;
    c->response_buf = wstrCatLen(c->response_buf, buf, len);
}
