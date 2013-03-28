// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "wheatserver.h"

// TODO: When reload config file if specify logfile is stdout,
// now code can't redirect back to stdout.
void logRedirect()
{
    if (Server.logfile && strcasecmp(Server.logfile, "stdout")) {
        FILE *fp;
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
        fp = freopen(Server.logfile, "a", stdout);
        if (!fp) {
            wheatLog(WHEAT_WARNING, "file redirect failed %s",
                    strerror(errno));
            halt(1);
        }
        setbuf(fp, NULL);
        fp = freopen(Server.logfile, "a", stderr);
        if (!fp) {
            wheatLog(WHEAT_WARNING, "file redirect failed %s",
                    strerror(errno));
            halt(1);
        }
        setbuf(fp, NULL);
    }
}

void wheatLogRaw(int level, const char *msg)
{
    const char *c = ".-* #";
    FILE *fp;
    char buf[64];
    int rawmode = (level & WHEAT_LOG_RAW);

    level &= 0xff; /* clear flags */
    if (level < Server.verbose) return;

    if (Server.logfile == NULL || !strcasecmp(Server.logfile, "stdout"))
        fp = stdout;
    else
        fp = fopen(Server.logfile, "a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        size_t off;
        struct timeval tv;

        gettimeofday(&tv,NULL);
        off = strftime(buf, sizeof(buf), "%d %b %H:%M:%S.", localtime(&tv.tv_sec));
        snprintf(buf+off, sizeof(buf)-off, "%03d", (int)tv.tv_usec/1000);
        fprintf(fp,"[%d] %s %c %s\n", (int)getpid(), buf, c[level], msg);
    }
    fflush(fp);

    if (fp != stdout) fclose(fp);
}

void wheatLog(int level, const char *fmt, ...)
{
    va_list ap;
    char msg[WHEATSERVER_MAX_LOG_LEN];
    char old_msg[WHEATSERVER_MAX_LOG_LEN];
    int ret, i, j;

    if ((level&0xff) < Server.verbose) return;

    va_start(ap, fmt);
    ret = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (ret >= 0 && ret < WHEATSERVER_MAX_LOG_LEN) {
        for (i = 0, j = 0; i < ret; ++i) {
            if (msg[i] == '\r') {
                old_msg[j++] = '\\';
                old_msg[j++] = 'r';
            } else if (msg[i] == '\n') {
                old_msg[j++] = '\\';
                old_msg[j++] = 'n';
            } else {
                old_msg[j++] = msg[i];
            }
        }
        old_msg[j] = '\0';
        wheatLogRaw(level, old_msg);
        return ;
    }
    wheatLogRaw(level, msg);
}
