// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_UTIL_H
#define WHEATSERVER_UTIL_H

/* Hash Type Dict */
struct dictType;
struct configuration;
extern struct dictType wstrDictType;
extern struct dictType sliceDictType;
extern struct dictType intDictType;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

int daemonize(int coredump);
void createPidFile();
void setProctitle(const char *title);
int getFileSizeAndMtime(int fd, off_t *len, time_t *m_time);
int isRegFile(const char *path);
int fromSameParentDir(wstr left, wstr right);
int ll2string(char *s, size_t len, long long value);
int string2ll(const char *s, size_t slen, long long *value);
size_t getIntLen(unsigned long i);

void setTimer(int milliseconds);

#endif
