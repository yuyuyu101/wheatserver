#ifndef _NET_H
#define _NET_H

#define NET_OK 0
#define NET_WRONG 1
#define NET_ERR_LEN 255

int wheatNonBlock(char *err, int fd);
int wheatTcpNoDelay(char *err, int fd);
int wheatTcpKeepAlive(char *err, int fd);
int wheatCloseOnExec(char *err, int fd);
int wheatTcpServer(char *err, char *bind_attr, int port);
int wheatTcpConnect(char *err, char *addr, int port);
int wheatTcpAccept(char *err, int s, char *ip, int *port);

#endif
