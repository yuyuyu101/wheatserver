#ifndef WHEATSERVER_PROTOCOL_H
#define WHEATSERVER_PROTOCOL_H

#include "../wheatserver.h"

struct protocol *spotProtocol(char *ip, int port, int fd);

#endif
