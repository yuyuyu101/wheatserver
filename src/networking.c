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
        wheatLog(WHEAT_VERBOSE, "Peer close file descriptor %d", fd);
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
            wheatLog(WHEAT_VERBOSE, "Receive RST, peer closed", strerror(errno));
            return WHEAT_WRONG;
        } else {
            wheatLog(WHEAT_NOTICE,
                "Error writing to client: %s", strerror(errno));
            return WHEAT_WRONG;
        }
    }
    return (int)nwritten;
}
