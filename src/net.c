#include <sys/socket.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "net.h"

static void wheatSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, NET_ERR_LEN, fmt, ap);
    va_end(ap);
}

static int wheatCreateSocket(char *err, int domain)
{
    int s, on = 1;

    if ((s = socket(domain, SOCK_STREAM, 0)) == -1) {
        wheatSetError(err, "creating socket: %s", strerror(errno));
        return NET_WRONG;
    }
     /* Make sure connection-intensive things like the benckmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        wheatSetError(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return NET_WRONG;
    }
    return s;
}

#define WHEAT_CONNECT_NONE 0
#define WHEAT_CONNECT_NONBLOCK 1
static int wheatTcpGenericConnect(char *err, char *addr, int port, int flags)
{
    int s;
    struct sockaddr_in sa;

    if ((s = wheatCreateSocket(err, AF_INET)) == NET_WRONG)
        return NET_WRONG;

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (!addr)
        addr = "127.0.0.1";
    if (inet_aton(addr, &sa.sin_addr) == 0) {
        struct hostent *he;

        he = gethostbyname(addr);
        if (he == NULL) {
            wheatSetError(err, "can't resolve: %s", addr);
            close(s);
            return NET_WRONG;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    if (flags & WHEAT_CONNECT_NONBLOCK) {
        if (wheatNonBlock(err,s) != NET_OK)
            return NET_WRONG;
    }
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
            flags & WHEAT_CONNECT_NONBLOCK)
            return s;

        wheatSetError(err, "connect: %s", strerror(errno));
        close(s);
        return NET_WRONG;
    }
    return s;
}

int wheatTcpConnect(char *err, char *addr, int port)
{
    return wheatTcpGenericConnect(err,addr,port,WHEAT_CONNECT_NONE);
}

int wheatTcpNonBlockConnect(char *err, char *addr, int port)
{
    return wheatTcpGenericConnect(err, addr, port, WHEAT_CONNECT_NONBLOCK);
}

static int wheatListen(char *err, int s, struct sockaddr *sa, socklen_t len) {
    if (bind(s, sa, len) == -1) {
        wheatSetError(err, "bind: %s", strerror(errno));
        close(s);
        return NET_WRONG;
    }

    /* Use a backlog of 512 entries. We pass 511 to the listen() call because
     * the kernel does: backlogsize = roundup_pow_of_two(backlogsize + 1);
     * which will thus give us a backlog of 512 entries */
    if (listen(s, 511) == -1) {
        wheatSetError(err, "listen: %s", strerror(errno));
        close(s);
        return NET_WRONG;
    }
    return NET_OK;
}

static int wheatGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len) {
    int fd;
    while(1) {
        fd = accept(s, sa, len);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN) {
                return NET_WRONG;
            }
            else {
                wheatSetError(err, "accept: %s", strerror(errno));
                return NET_WRONG;
            }
        }
        break;
    }
    return fd;
}

int wheatNonBlock(char *err, int fd)
{
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) < 0 ) {
        wheatSetError(err, "fcntl(F_GETFL): %s", strerror(errno));
        return NET_WRONG;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        wheatSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
        return NET_WRONG;
    }
    return NET_OK;
}

int wheatCloseOnExec(char *err, int fd)
{
    int     flags;

    if ((flags = fcntl(fd, F_GETFD, 0)) < 0) {
        wheatSetError(err, "fcntl(F_GETFL): %s", strerror(errno));
        return NET_WRONG;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        wheatSetError(err, "fcntl(F_SETFL, FD_CLOEXEC): %s", strerror(errno));
        return NET_WRONG;
    }
    return NET_OK;
}

int wheatSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        wheatSetError(err, "setsockopt SO_SNDBUF: %s", strerror(errno));
        return NET_WRONG;
    }
    return NET_OK;
}

int wheatTcpNoDelay(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1)
    {
        wheatSetError(err, "setsockopt TCP_NODELAY: %s", strerror(errno));
        return NET_WRONG;
    }
    return NET_OK;
}

int wheatTcpKeepAlive(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        wheatSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return NET_WRONG;
    }
    return NET_OK;
}

/* sizeof(ip) must larger than INET_ADDRSTRLEN(16) */
int wheatTcpAccept(char *err, int s, char *ip, int *port) {
    int fd;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    if ((fd = wheatGenericAccept(err, s, (struct sockaddr*)&sa,&salen)) == NET_WRONG)
        return NET_WRONG;

    if (ip) inet_ntop(AF_INET, &sa.sin_addr, ip, INET_ADDRSTRLEN);
    if (port) *port = ntohs(sa.sin_port);
    return fd;
}

int wheatTcpServer(char *err, char *bind_addr, int port){
    int s;
    struct sockaddr_in sa;

    if ((s = wheatCreateSocket(err, AF_INET)) == NET_WRONG)
        return NET_WRONG;

    memset(&sa,0,sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind_addr && inet_pton(AF_INET, bind_addr, &sa.sin_addr) != 1) {
        wheatSetError(err, "wheatTcpServer: invalid bind address");
        close(s);
        return NET_WRONG;
    }

    if (wheatListen(err, s, (struct sockaddr*)&sa, sizeof(sa)) == NET_WRONG) {
        wheatSetError(err, "wheatTcpServer: listen failed");
        close(s);
        return NET_WRONG;
    }
    return s;
}
