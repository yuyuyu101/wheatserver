// Implementation of the portable APIs on different platforms
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_PORTABLE_H
#define WHEATSERVER_PORTABLE_H

// portable for sendfile(2), support BSD and linux.
// return the length of data sent or -1 means error occurred, return 0 if
// outer_fd is non-blocking means EAGAIN errno.
ssize_t portable_sendfile(int out_fd, int in_fd, off_t, off_t len);

/* Check if we can use setproctitle().
 * BSD systems have support for it, we provide an implementation for
 * Linux and osx. */
#if (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__)
#define USE_SETPROCTITLE
#endif

#if (defined __linux || defined __APPLE__)
#define USE_SETPROCTITLE
void setproctitle(const char *fmt, ...);
#endif

#endif
