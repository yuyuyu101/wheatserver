// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>

#include "wheatserver.h"

struct dictType wstrDictType;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1)
{
    int full_pipe[2];
    if (pipe(full_pipe) != 0) {
        wheatLog(WHEAT_WARNING, "Create pipe failed: %s", strerror(errno));
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[0]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[0], Server.neterr);
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[1]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[1], Server.neterr);
        halt(1);
    }
    *fd0 = full_pipe[0];
    *fd1 = full_pipe[1];
}


unsigned int dictWstrHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, wstrlen((char*)key));
}

unsigned int dictWstrCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, wstrlen((char*)key));
}

int dictWstrKeyCompare(const void *key1, const void *key2)
{
    int l1,l2;

    l1 = wstrlen((wstr)key1);
    l2 = wstrlen((wstr)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictWstrDestructor(void *val)
{
    wstrFree(val);
}

struct dictType wstrDictType = {
    dictWstrHash,               /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictWstrKeyCompare,         /* key compare */
    dictWstrDestructor,         /* key destructor */
    dictWstrDestructor,         /* val destructor */
};

unsigned int dictSliceHash(const void *key) {
    const struct slice *s = key;
    return dictGenHashFunction(s->data, (int)s->len);
}

unsigned int dictSliceCaseHash(const void *key) {
    const struct slice *s = key;
    return dictGenCaseHashFunction(s->data, (int)s->len);
}

int dictSliceKeyCompare(const void *key1, const void *key2)
{
    const struct slice *s1 = key1, *s2 = key2;
    size_t l1,l2;

    l1 = s1->len;
    l2 = s2->len;
    if (l1 != l2) return 0;
    return memcmp(s1->data, s2->data, l1) == 0;
}

void dictSliceDestructor(void *val)
{
    sliceFree(val);
}

struct dictType sliceDictType = {
    dictSliceHash,               /* hash function */
    NULL,                        /* key dup */
    NULL,                        /* val dup */
    dictSliceKeyCompare,         /* key compare */
    dictSliceDestructor,         /* key destructor */
    dictSliceDestructor,         /* val destructor */
};

unsigned int dictEncIntHash(const void *key)
{
    char buf[32];
    int len;

    len = ll2string(buf, 32, (intptr_t)key);
    return dictGenHashFunction((unsigned char*)buf, len);
}

int dictEncIntKeyCompare(const void *key1, const void *key2)
{
    return key1 == key2;
}

struct dictType intDictType = {
    dictEncIntHash,             /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictEncIntKeyCompare,       /* key compare */
    NULL,                       /* key destructor */
    NULL,                       /* val destructor */
};


int daemonize(int dump_core)
{
    int status;
    pid_t pid, sid;
    int fd;

    pid = fork();
    switch (pid) {
        case -1:
            wheatLog(WHEAT_WARNING, "fork() failed: %s", strerror(errno));
            return WHEAT_WRONG;

        case 0:
            break;

        default:
            /* parent terminates */
            _exit(0);
    }

    /* 1st child continues and becomes the session leader */

    sid = setsid();
    if (sid < 0) {
        wheatLog(WHEAT_WARNING, "setsid() failed: %s", strerror(errno));
        return WHEAT_WRONG;
    }

    pid = fork();
    switch (pid) {
        case -1:
            wheatLog(WHEAT_WARNING, "fork() failed: %s", strerror(errno));
            return WHEAT_WRONG;

        case 0:
            break;

        default:
            /* 1st child terminates */
            _exit(0);
    }

    /* 2nd child continues */

    /* change working directory */
    if (dump_core == 0) {
        status = chdir("/");
        if (status < 0) {
            wheatLog(WHEAT_WARNING, "chdir(\"/\") failed: %s", strerror(errno));
            return WHEAT_WRONG;
        }
    }

    /* clear file mode creation mask */
    umask(0);

    /* redirect stdin, stdout and stderr to "/dev/null" */

    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        wheatLog(WHEAT_WARNING, "open(\"/dev/null\") failed: %s", strerror(errno));
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDIN_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDIN) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDOUT_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDOUT) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDERR_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDERR) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    if (fd > STDERR_FILENO) {
        status = close(fd);
        if (status < 0) {
            wheatLog(WHEAT_WARNING, "close(%d) failed: %s", fd, strerror(errno));
            return WHEAT_WRONG;
        }
    }

    return WHEAT_OK;
}

void createPidFile()
{
    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(Server.pidfile, "w");
    if (fp) {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    }
}

void setTimer(int milliseconds)
{
    struct itimerval it;

    /* Will stop the timer if period is 0. */
    it.it_value.tv_sec = milliseconds / 1000;
    it.it_value.tv_usec = (milliseconds % 1000) * 1000;
    it.it_interval.tv_sec = it.it_value.tv_sec;
    it.it_interval.tv_usec = it.it_value.tv_usec;
    setitimer(ITIMER_REAL, &it, NULL);
}

int getFileSizeAndMtime(int fd, off_t *len, time_t *m_time)
{
    ASSERT(fd > 0);
    struct stat stat;
    int ret = fstat(fd, &stat);
    if (ret == -1)
        return WHEAT_WRONG;
    if (len) *len = stat.st_size;
    if (m_time) *m_time = stat.st_mtime;
    return WHEAT_OK;
}

int isRegFile(const char *path)
{
    ASSERT(path);
    struct stat stat;
    int ret = lstat(path, &stat);
    if (ret == -1)
        return WHEAT_WRONG;
    return S_ISREG(stat.st_mode) == 0 ? WHEAT_WRONG : WHEAT_OK;
}

int fromSameParentDir(wstr parent, wstr child)
{
    if (wstrlen(parent) > wstrlen(child))
        return 0;
    return memcmp(parent, child, wstrlen(parent)) == 0;
}

int ll2string(char *s, size_t len, long long value)
{
    char buf[32], *p;
    unsigned long long v;
    size_t l;

    if (len == 0) return 0;
    v = (value < 0) ? -value : value;
    p = buf+31; /* point to the last character */
    do {
        *p-- = '0'+(v%10);
        v /= 10;
    } while(v);
    if (value < 0) *p-- = '-';
    p++;
    l = 32-(p-buf);
    if (l+1 > len) l = len-1; /* Make sure it fits, including the nul term */
    memcpy(s,p,l);
    s[l] = '\0';
    return l;
}

/* Convert a string into a long long. Returns 1 if the string could be parsed
 * into a (non-overflowing) long long, 0 otherwise. The value will be set to
 * the parsed value when appropriate. */
int string2ll(const char *s, size_t slen, long long *value)
{
    const char *p = s;
    size_t plen = 0;
    int negative = 0;
    unsigned long long v;

    if (plen == slen)
        return WHEAT_WRONG;

    /* Special case: first and only digit is 0. */
    if (slen == 1 && p[0] == '0') {
        if (value != NULL) *value = 0;
        return WHEAT_OK;
    }

    if (p[0] == '-') {
        negative = 1;
        p++; plen++;

        /* Abort on only a negative sign. */
        if (plen == slen)
            return WHEAT_WRONG;
    }

    /* First digit should be 1-9, otherwise the string should just be 0. */
    if (p[0] >= '1' && p[0] <= '9') {
        v = p[0]-'0';
        p++; plen++;
    } else if (p[0] == '0' && slen == 1) {
        *value = 0;
        return WHEAT_OK;
    } else {
        return WHEAT_WRONG;
    }

    while (plen < slen && p[0] >= '0' && p[0] <= '9') {
        if (v > (ULLONG_MAX / 10)) /* Overflow. */
            return WHEAT_WRONG;
        v *= 10;

        if (v > (ULLONG_MAX - (p[0]-'0'))) /* Overflow. */
            return WHEAT_WRONG;
        v += p[0]-'0';

        p++; plen++;
    }

    /* Return if not all bytes were used. */
    if (plen < slen)
        return WHEAT_WRONG;

    if (negative) {
        if (v > ((unsigned long long)(-(LLONG_MIN+1))+1)) /* Overflow. */
            return WHEAT_WRONG;
        if (value != NULL) *value = -v;
    } else {
        if (v > LLONG_MAX) /* Overflow. */
            return WHEAT_WRONG;
        if (value != NULL) *value = v;
    }
    return WHEAT_OK;
}

size_t getIntLen(unsigned long i)
{
    size_t len = 1;
    while (i /= 10) {
        len++;
    }
    return len;
}
