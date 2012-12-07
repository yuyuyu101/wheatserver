#include "wheatserver.h"

char *charsDup(const char *s)
{
    int len = strlen(s) + 1;
    char *new_s = malloc(len);
    if (new_s == NULL)
        return NULL;
    memcpy(new_s, s, len);
    return new_s;
}

int parseOnOrOff(const char *s)
{
    int len = strlen(s) + 1;
    if (memcmp("on", s, len)) {
        return 1;
    } else if (memcmp("off", s, len)) {
        return 0;
    } else
        return -1;
}

void applyConfig(wstr config)
{
    int count;
    wstr *lines = wstrNewSplit(config, "\n", 1, &count);
    char *err;

    int i;
    for (i = 0; i < count; i++) {
        wstr line = wstrStrip(lines[i], "\t\n\r ");
        if (line[0] == '#' || line[0] == '\0')
            continue;

        int args;
        wstr *argvs = wstrNewSplit(line, " ", 1, &args);
        if (!wstrCmpChars(argvs[0], "port", 4) && args == 2) {
            Server.port = atoi(argvs[1]);
            if (Server.port <= 0) {
                err = "nagetive port";
                goto loaderr;
            }
        } else if (!wstrCmpChars(argvs[0], "bind-addr", 9) && args == 2) {
            Server.bind_addr = charsDup(argvs[1]);
            if (Server.bind_addr == NULL) {
                err = "bind_addr is NULL";
                goto loaderr;
            }
        } else if (!wstrCmpChars(argvs[0], "logfile", 7) && args == 2) {
            Server.logfile = charsDup(argvs[1]);
            if (Server.logfile == NULL) {
                err = "logfile is NULL";
                goto loaderr;
            }
        } else if (!wstrCmpChars(argvs[0], "logfile-level", 13) && args == 2) {
            if (!wstrCmpChars(argvs[1], "DEBUG", 5))
                Server.verbose = WHEAT_DEBUG;
            else if (!wstrCmpChars(argvs[1], "VERBOSE", 7))
                Server.verbose = WHEAT_VERBOSE;
            else if (!wstrCmpChars(argvs[1], "NOTICE", 6))
                Server.verbose = WHEAT_NOTICE;
            else if (!wstrCmpChars(argvs[1], "WARNING", 7))
                Server.verbose = WHEAT_WARNING;
            else {
                err = "log level incorrect.";
                goto loaderr;
            }
        } else if (!wstrCmpChars(argvs[0], "worker-number", 12) && args == 2) {
            Server.worker_number = atoi(argvs[1]);
            if (Server.worker_number > 0) {
                err = "worker-number can't be nagetive";
                goto loaderr;
            }
        }
        wstrFreeSplit(argvs, args);
    }
    wstrFreeSplit(lines, count);
    return ;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", i+1);
    fprintf(stderr, ">>> '%s'\n", lines[i]);
    fprintf(stderr, "%s\n", err);
    halt(1);
}

void loadConfigFile(const char *filename, char *options)
{
    wstr config = wstrEmpty();
    char line[WHEATSERVER_CONFIGLINE_MAX+1];
    if (filename) {
        FILE *fp;
        if ((fp = fopen(filename, "r")) == NULL) {
            wheatLog(WHEAT_WARNING,
                "Fatal error, can't open config file '%s'", filename);
            halt(1);
        }
        while(fgets(line, WHEATSERVER_CONFIGLINE_MAX+1, fp) != NULL)
            config = wstrCat(config, line);
        if (fp != stdin)
            fclose(fp);
    }
    if (options) {
        config = wstrCat(config,"\n");
        config = wstrCat(config, options);
    }

    applyConfig(config);
    wstrFree(config);
}

void statConfigByName(const char *n, char *result, int len)
{
    wstr name = wstrNew(n);

    if (!wstrCmpChars(name, "port", 4)) {
        snprintf(result, len, "%d", Server.port);
    } else if (!wstrCmpChars(name, "bind-addr", 9)) {
        snprintf(result, len, "%s", Server.bind_addr);
    } else if (!wstrCmpChars(name, "logfile", 7)) {
        snprintf(result, len, "%s", Server.logfile);
    } else if (!wstrCmpChars(name, "logfile-level", 13)) {
        if (Server.verbose == 0)
            snprintf(result, len, "DEBUG");
        else if (Server.verbose == 1)
            snprintf(result, len, "VERBOSE");
        else if (Server.verbose == 2)
            snprintf(result, len, "NOTICE");
        else if (Server.verbose == 3)
            snprintf(result, len, "WARNING");
        else
            snprintf(result, len, "UNKNOWN");
    } else
        snprintf(result, len, "UNKNOWN");
    wstrFree(name);
}

void printServerConfig()
{
    printf("bind_addr:%s\n", Server.bind_addr);
    printf("logfile:%s\n", Server.logfile);
    printf("port:%d\n", Server.port);
    printf("verbose:%d\n", Server.verbose);
}


