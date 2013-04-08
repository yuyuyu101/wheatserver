// Basic TCP socket stuff made a bit less boring
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_NET_H
#define WHEATSERVER_NET_H

#define NET_OK 0
#define NET_WRONG -1
#define NET_ERR_LEN 255

int wheatNonBlock(char *err, int fd);
int wheatTcpNoDelay(char *err, int fd);
int wheatTcpKeepAlive(char *err, int fd);
int wheatCloseOnExec(char *err, int fd);
int wheatTcpServer(char *err, char *bind_attr, int port);
int wheatTcpConnect(char *err, char *addr, int port);
int wheatTcpNonBlockConnect(char *err, char *addr, int port);
int wheatTcpAccept(char *err, int s, char *ip, int *port);

#endif
