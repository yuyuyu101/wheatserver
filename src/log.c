#include "wheatserver.h"

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
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"[%d] %s %c %s\n", (int)getpid(), buf, c[level], msg);
    }
    fflush(fp);

    if (fp != stdout) fclose(fp);
}

void wheatLog(int level, const char *fmt, ...)
{
    va_list ap;
    char msg[WHEATSERVER_MAX_LOG_LEN];

    if ((level&0xff) < Server.verbose) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    wheatLogRaw(level,msg);
}
