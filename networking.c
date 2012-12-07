#include "wheatserver.h"


/* because readBulkFrom call wstrMakeRoom realloc memory pointer,
 * pass a pointer point pointer is needed.
 * return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN */
int readBulkFrom(int fd, wstr *clientbuf)
{
    int nread, readlen;
    size_t qblen;
    wstr buf = *clientbuf;

    readlen = WHEAT_IOBUF_LEN;

    qblen = wstrlen(buf);
    if (wstrfree(buf) * 2 > readlen)
        readlen = wstrfree(buf);
    buf = wstrMakeRoom(buf, readlen);

    // Important ! buf may changed
    *clientbuf = buf;

    nread = read(fd, buf+qblen, readlen);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            wheatLog(WHEAT_VERBOSE, "Reading from fd %d: %s", fd, strerror(errno));
            return -1;
        }
    } else if (nread == 0) {
        wheatLog(WHEAT_VERBOSE, "fd closed");
        return -1;
    }
    if (nread) {
        wstrupdatelen(buf, qblen+nread);
    }
    return nread;
}

int writeBulkTo(int fd, wstr *clientbuf)
{
    wstr buf = *clientbuf;
    int bufpos = 0, nwritten = 0, totalwritten;
    while(bufpos <= wstrlen(buf)) {
        nwritten = write(fd, buf+bufpos, wstrlen(buf)-bufpos);
        if (nwritten <= 0)
            break;
        bufpos += nwritten;
        totalwritten += nwritten;
    }
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            wheatLog(WHEAT_VERBOSE,
                "Error writing to client: %s", strerror(errno));
            return -1;
        }
    }
    return nwritten;
}
