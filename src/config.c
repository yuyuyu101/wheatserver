#include "wheatserver.h"

static struct enumIdName Verbose[] = {
    {WHEAT_DEBUG, "DEBUG"}, {WHEAT_VERBOSE, "VERBOSE"},
    {WHEAT_NOTICE, "NOTICE"}, {WHEAT_WARNING, "WARNING"},
    {-1, NULL}
};

static struct enumIdName Workers[] = {
    {0, "SyncWorker"}, {1, "AsyncWorker"}
};

// Attention: If modify configTable, three places should be attention to.
// 1. initGlobalServerConfig() in wheatserver.c
// 2. wheatserver.conf
// 3. fillServerConfig() below
struct configuration configTable[] = {
    //  name   |    args   |      validator      |      default       |
    //  helper |    flags

    // Master Configuration
    {"bind-addr",         2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"port",              2, unsignedIntValidator, {.val=WHEAT_SERVERPORT},
        NULL,                   INT_FORMAT},
    {"worker-number",     2, unsignedIntValidator, {.val=2},
        (void *)1024,           INT_FORMAT},
    {"worker-type",       2, enumValidator,        {.enum_ptr=&Workers[0]},
        &Workers[0],            ENUM_FORMAT},
    {"logfile",           2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"logfile-level",     2, enumValidator,        {.enum_ptr=&Verbose[0]},
        &Verbose[0],            ENUM_FORMAT},
    {"daemon",            2, boolValidator,        {.val=0},
        NULL,                   BOOL_FORMAT},
    {"pidfile",           2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"max-buffer-size",   2, unsignedIntValidator, {.val=0},
        NULL,                   INT_FORMAT},
    {"stat-bind-addr",    2, stringValidator,      {.ptr=WHEAT_STATS_ADDR},
        (void *)WHEAT_NOTFREE, STRING_FORMAT},
    {"stat-port",         2, unsignedIntValidator, {.val=WHEAT_STATS_PORT},
        NULL,                   INT_FORMAT},
    {"stat-refresh-time", 2, unsignedIntValidator, {.val=WHEAT_STAT_REFRESH},
        NULL,                   INT_FORMAT},
    {"timeout-seconds",   2, unsignedIntValidator, {.val=WHEATSERVER_TIMEOUT},
        (void *)300,            INT_FORMAT},

    // Http
    {"access-log",        2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},

    // WSGI Configuration
    {"app-module-path",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"app-module-name",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"app-name",          2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
};

void fillServerConfig()
{
    struct configuration *conf = &configTable[0];

    Server.bind_addr = conf->target.ptr;
    conf++;
    Server.port = conf->target.val;
    conf++;
    Server.worker_number = conf->target.val;
    conf++;
    Server.worker_type = conf->target.enum_ptr->name;
    conf++;
    Server.logfile = conf->target.ptr;
    conf++;
    Server.verbose = conf->target.enum_ptr->id;
    conf++;
    Server.daemon = conf->target.val;
    conf++;
    Server.pidfile = conf->target.ptr;
    conf++;
    Server.max_buffer_size = conf->target.val;
    conf++;
    Server.stat_addr = conf->target.ptr;
    conf++;
    Server.stat_port = conf->target.val;
    conf++;
    Server.stat_refresh_seconds = conf->target.val;
    conf++;
    Server.worker_timeout = conf->target.val;
}

static void extraValidator()
{
    ASSERT(Server.stat_refresh_seconds < Server.worker_timeout);
    ASSERT(Server.port && Server.stat_port);
}

struct configuration *getConfiguration(const char *name)
{
    int len = sizeof(configTable) / sizeof(struct configuration);
    int i;
    for (i = 0; i < len; i++) {
        if (!strncasecmp(name, configTable[i].name, strlen(name))) {
            return &configTable[i];
        }
    }
    return NULL;
}

void applyConfig(wstr config)
{
    int count, args;
    wstr *lines = wstrNewSplit(config, "\n", 1, &count);
    char *err;
    struct configuration *conf;

    int i;
    for (i = 0; i < count; i++) {
        wstr line = wstrStrip(lines[i], "\t\n\r ");
        if (line[0] == '#' || line[0] == '\0')
            continue;

        wstr *argvs = wstrNewSplit(line, " ", 1, &args);

        if ((conf = getConfiguration(argvs[0])) == NULL) {
            err = "Unknown configuration name";
            goto loaderr;
        }
        if (args != conf->args) {
            err = "Incorrect args";
            goto loaderr;
        }
        if (conf->validator(conf, argvs[0], argvs[1]) != VALIDATE_OK) {
            err = "Validate Failed";
            goto loaderr;
        }

        wstrFreeSplit(argvs, args);
    }
    wstrFreeSplit(lines, count);
    fillServerConfig();
    return ;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", i+1);
    fprintf(stderr, ">>> '%s'\n", lines[i]);
    fprintf(stderr, "Reason: %s\n", err);
    halt(1);
}

void loadConfigFile(const char *filename, char *options)
{
    wstr config = wstrEmpty();
    char line[WHEATSERVER_CONFIGLINE_MAX+1];
    if (filename[0] != '\0') {
        FILE *fp;
        if ((fp = fopen(filename, "r")) == NULL) {
            wheatLog(WHEAT_WARNING,
                "Fatal error, can't open config file '%s'", filename);
            halt(1);
        }
        while(fgets(line, WHEATSERVER_CONFIGLINE_MAX, fp) != NULL)
            config = wstrCat(config, line);
        if (fp != stdin)
            fclose(fp);
    }
    if (options) {
        config = wstrCat(config,"\n");
        config = wstrCat(config, options);
    }

    applyConfig(config);
    extraValidator();
    printServerConfig();
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
        if (Server.verbose == WHEAT_DEBUG)
            snprintf(result, len, "DEBUG");
        else if (Server.verbose == WHEAT_VERBOSE)
            snprintf(result, len, "VERBOSE");
        else if (Server.verbose == WHEAT_NOTICE)
            snprintf(result, len, "NOTICE");
        else if (Server.verbose == WHEAT_WARNING)
            snprintf(result, len, "WARNING");
        else
            snprintf(result, len, "UNKNOWN");
    } else
        snprintf(result, len, "UNKNOWN");
    wstrFree(name);
}

static ssize_t constructConfigFormat(struct configuration *conf, char *buf, size_t len)
{
    ssize_t ret = 0;
    if (conf->format == STRING_FORMAT)
        ret = snprintf(buf, len, "%s: %s", conf->name, conf->target.ptr);
    else if (conf->format == INT_FORMAT)
        ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
    else if (conf->format == ENUM_FORMAT) {
        ret = snprintf(buf, len, "%s: %s", conf->name, conf->target.enum_ptr->name);
    } else if (conf->format == BOOL_FORMAT) {
        ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
    }
    return ret;
}

void configCommand(struct masterClient *c)
{
    struct configuration *conf = NULL;
    char buf[255];
    ssize_t len;
    conf = getConfiguration(c->argv[1]);
    if (conf == NULL) {
        len = snprintf(buf, 255, "No Correspond Configuration");
    } else {
        len = constructConfigFormat(conf, buf, 255);
    }
    replyMasterClient(c, buf, len);
}

void printServerConfig()
{
    wheatLog(WHEAT_DEBUG, "---- Now Configuration are ----");
    int size = sizeof(configTable) / sizeof(struct configuration);
    int i;
    struct configuration *conf = &configTable[0];
    char buf[255];
    for (i = 0; i < size; i++) {
        constructConfigFormat(conf, buf, 255);
        wheatLog(WHEAT_DEBUG, "%s", buf);
        conf++;
    }
    wheatLog(WHEAT_DEBUG, "-------------------------------");
}
