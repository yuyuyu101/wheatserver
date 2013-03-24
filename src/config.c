#include "wheatserver.h"

static struct enumIdName Verbose[] = {
    {WHEAT_DEBUG, "DEBUG"}, {WHEAT_VERBOSE, "VERBOSE"},
    {WHEAT_NOTICE, "NOTICE"}, {WHEAT_WARNING, "WARNING"},
    {-1, NULL}
};

static struct enumIdName Workers[] = {
    {0, "SyncWorker"}, {1, "AsyncWorker"}
};

/* Configuration Validator */
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);
int listValidator(struct configuration *conf, const char *key, const char *val);

// configTable is immutable after worker setuped in *Worker Process*
// Attention: If modify configTable, three places should be attention to.
// 1. initGlobalServerConfig() in wheatserver.c
// 2. wheatserver.conf
// 3. fillServerConfig() below
struct configuration configTable[] = {
    //  name   |    args   |      validator      |      default       |
    //  helper |    flags

    // Master Configuration
    {"protocol",          2, stringValidator,      {.ptr=WHEAT_PROTOCOL_DEFAULT},
        (void *)WHEAT_NOTFREE,  STRING_FORMAT},
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
    {"logfile-level",     2, enumValidator,        {.enum_ptr=&Verbose[2]},
        &Verbose[0],            ENUM_FORMAT},
    {"daemon",            2, boolValidator,        {.val=0},
        NULL,                   BOOL_FORMAT},
    {"pidfile",           2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"max-buffer-size",   2, unsignedIntValidator, {.val=WHEAT_MAX_BUFFER_SIZE},
        (void *)WHEAT_BUFLIMIT, INT_FORMAT},
    {"stat-bind-addr",    2, stringValidator,      {.ptr=WHEAT_STATS_ADDR},
        (void *)WHEAT_NOTFREE, STRING_FORMAT},
    {"stat-port",         2, unsignedIntValidator, {.val=WHEAT_STATS_PORT},
        NULL,                   INT_FORMAT},
    {"stat-refresh-time", 2, unsignedIntValidator, {.val=WHEAT_STAT_REFRESH},
        NULL,                   INT_FORMAT},
    {"timeout-seconds",   2, unsignedIntValidator, {.val=WHEATSERVER_TIMEOUT},
        (void *)300,            INT_FORMAT},
    {"mbuf-size",         2, unsignedIntValidator, {.val=WHEAT_MBUF_SIZE},
        (void *)WHEAT_BUFLIMIT, INT_FORMAT},

    // Http
    {"access-log",        2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"document-root",     2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},

    // Redis
    {"redis-servers",     WHEAT_ARGS_NO_LIMIT,listValidator, {.ptr=NULL},
        NULL,                   LIST_FORMAT},
    {"backup-size",       2, unsignedIntValidator, {.val=1},
        NULL,                   INT_FORMAT},
    {"redis-timeout",     2, unsignedIntValidator, {.val=1000},
        NULL,                   INT_FORMAT},

    // WSGI Configuration
    {"app-project-path",  2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"app-module-name",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"app-name",          2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},

    // Static File Configuration
    {"static-file-dir",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"file-maxsize",      2, unsignedIntValidator, {.val=WHEAT_MAX_FILE_LIMIT},
        NULL,                   INT_FORMAT},
    {"allowed-extension", 2, stringValidator,      {.ptr=WHEAT_ASTERISK},
        (void *)WHEAT_NOTFREE,  STRING_FORMAT},
};

void fillServerConfig()
{
    struct configuration *conf = &configTable[0];

    conf++;
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
    conf++;
    Server.mbuf_size = conf->target.val;
}

/* ========== Configuration Validator/Print Area ========== */

int stringValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);

    if (conf->target.ptr && !conf->helper) {
        wfree(conf->target.ptr);
        conf->helper = NULL;
    }
    conf->target.ptr = strdup(val);
    return VALIDATE_OK;
}

int listValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    if (conf->target.ptr) {
        freeList(conf->target.ptr);
    }

    conf->target.ptr = (struct list *)val;
    return VALIDATE_OK;
}

int unsignedIntValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    int i, digit, value;
    intptr_t max_limit = 0;

    if (conf->helper)
        max_limit = (intptr_t)conf->helper;

    for (i = 0; val[i] != '\0'; i++) {
        digit = val[i] - 48;
        if (digit < 0 || digit > 9)
            return VALIDATE_WRONG;
        // Avoid too long int value
        if (i >= 10)
            return VALIDATE_WRONG;
    }

    value = atoi(val);
    if (max_limit != 0 && value > max_limit)
        return VALIDATE_WRONG;
    conf->target.val = value;
    return VALIDATE_OK;
}

int enumValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    ASSERT(conf->helper);

    struct enumIdName *sentinel = (struct enumIdName *)conf->helper;
    while (sentinel->name) {
        if (strncasecmp(val, sentinel->name, strlen(sentinel->name)) == 0) {
            conf->target.enum_ptr = sentinel;
            return VALIDATE_OK;
        }
        else if (sentinel == NULL)
            break ;
        sentinel++;
    }

    return VALIDATE_WRONG;
}

int boolValidator(struct configuration *conf, const char *key, const char *val)
{
    size_t len = strlen(val) + 1;
    if (memcmp("on", val, len) == 0) {
        conf->target.val = 1;
    } else if (memcmp("off", val, len) == 0) {
        conf->target.val = 0;
    } else
        return VALIDATE_WRONG;
    return VALIDATE_OK;
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

static struct list *handleList(wstr *lines, int *i, int count)
{
    int pos = *i + 1;
    wstr line = NULL;
    struct list *l = createList();
    listSetFree(l, (void(*)(void *))wstrFree);
    while (pos < count) {
        line = wstrStrip(lines[pos], "\t\n\r ");
        int len = 0;
        int line_valid = 0;
        while (len < wstrlen(line)) {
            char ch = line[len];
            switch(ch) {
                case ' ':
                    len++;
                    break;
                case '-':
                    if (!line_valid)
                        line_valid = 1;
                    else
                        goto handle_error;
                    len++;
                    break;
                default:
                    if (!line_valid)
                        goto handle_error;
                    appendToListTail(l, wstrNew(&line[len]));
                    len = wstrlen(line);
                    break;
            }
        }
        pos++;
    }

handle_error:
    pos--;
    *i = pos;
    return l;
}

void applyConfig(wstr config)
{
    int count, args;
    wstr *lines = wstrNewSplit(config, "\n", 1, &count);
    char *err = NULL;
    struct configuration *conf;
    void *ptr = NULL;

    int i = 0;
    if (lines == NULL) {
        err = "Memory alloc failed";
        goto loaderr;
    }
    for (i = 0; i < count; i++) {
        wstr line = wstrStrip(lines[i], "\t\n\r ");
        if (line[0] == '#' || line[0] == '\0' || line[0] == ' ')
            continue;

        wstr *argvs = wstrNewSplit(line, " ", 1, &args);
        if (argvs == NULL) {
            err = "Memory alloc failed";
            goto loaderr;
        }

        if ((conf = getConfiguration(argvs[0])) == NULL) {
            err = "Unknown configuration name";
            goto loaderr;
        }

        ptr = argvs[1];
        if (args == 1 && conf->format == LIST_FORMAT) {
            struct list *l = handleList(lines, &i, count);
            if (!l) {
                err = "parse list failed";
                goto loaderr;
            }
            ptr = l;
        } else if (args != conf->args && args == 2) {
            int j;
            for (j = 2; j < args; ++j) {
                argvs[1] = wstrCatLen(argvs[1], argvs[j], wstrlen(argvs[j]));
                if (!argvs[1]) {
                    err = "Unknown configuration name";
                    goto loaderr;
                }
            }
        }
        if (args != conf->args && conf->args != WHEAT_ARGS_NO_LIMIT) {
            err = "Incorrect args";
            goto loaderr;
        }
        if (conf->validator(conf, argvs[0], ptr) != VALIDATE_OK) {
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

void loadConfigFile(const char *filename, char *options, int test)
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
    printServerConfig(test);
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
    int ret = 0, pos = 0;
    struct listIterator *iter = NULL;
    wstr line = NULL;
    struct listNode *node = NULL;
    switch(conf->format) {
        case STRING_FORMAT:
            ret = snprintf(buf, len, "%s: %s", conf->name, (char *)conf->target.ptr);
            break;
        case INT_FORMAT:
            ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
            break;
        case ENUM_FORMAT:
            ret = snprintf(buf, len, "%s: %s", conf->name, conf->target.enum_ptr->name);
            break;
        case BOOL_FORMAT:
            ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
            break;
        case LIST_FORMAT:
            ret = snprintf(buf, len, "%s: ", conf->name);
            pos = ret;
            if (!conf->target.ptr)
                break;
            iter = listGetIterator((struct list*)(conf->target.ptr), START_HEAD);
            while ((node = listNext(iter)) != NULL) {
                line = (wstr)listNodeValue(node);
                ret = snprintf(buf+pos, len, "%s\t", line);
                if (ret < 0 || ret > len-pos)
                    return pos;
                pos += ret;
            }
            freeListIterator(iter);
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

void printServerConfig(int test)
{
    int level = test ? WHEAT_NOTICE : WHEAT_DEBUG;
    wheatLog(level, "---- Now Configuration are ----");
    int size = sizeof(configTable) / sizeof(struct configuration);
    int i;
    struct configuration *conf = &configTable[0];
    char buf[255];
    for (i = 0; i < size; i++) {
        constructConfigFormat(conf, buf, 255);
        wheatLog(level, "%s", buf);
        conf++;
    }
    wheatLog(level, "-------------------------------");
}
